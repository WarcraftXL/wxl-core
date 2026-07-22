// Legion-style height-based terrain texture blending (MTXP + "_h" textures) for split-ADT maps.
// Copyright (C) 2026 WarcraftXL
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.

// MECHANISM (draw-leaf detour + runtime PS patch;
//   The 3.3.5 terrain surface binds its pixel shader per (bucket, layerCount) OUTSIDE the per-chunk
//   draw leaf: CMapRenderChunk::SetShaders refreshes the active-PS table (kActiveTerrainPs[0..3]),
//   the bucket loop writes slot[n-1] at GxRs 0x4E, then calls the leaf (kSurfaceChunkDrawShader)
//   per chunk; the pre-DIP GxState flush applies whatever wrapper sits in the 0x4E cache. So a
//   PRE-detour on the leaf that (a) binds the 4 "_h" height textures at s9..s12 (GxRs 0x1E..0x21),
//   (b) uploads c22 = heightScale.xyzw / c23 = heightOffset.xyzw / c24.x = sharpness, and (c) writes
//   its own CGxShader wrapper into GxRs 0x4E -- then runs the untouched original and restores the
//   stock wrapper -- owns exactly that one chunk's pixel shader with zero other state disturbed.
//
//   The replacement PS is not authored from scratch: the STOCK permutation the draw would use is
//   read out of its CGxShader wrapper (bytecode +0x50 / length +0x4C), disassembled with
//   D3DDisassemble (grasswind precedent), and surgically injected right after its combined-alpha
//   texld: the injected block samples the four "_h" textures with each layer's OWN texcoord, builds
//   the Legion weights
//       base = (1 - sat(a1+a2+a3), a1, a2, a3)
//       w    = base * (h * heightScale + heightOffset)          // c22 / c23
//       w   *= 1 - sat((max4(w) - w) * sharpness)               // c24.x, tunable (default 1.0)
//       w   /= dot(w, 1)
//   and writes w1..w3 back into the alpha register's .rgb (shadow in .a preserved). The stock code
//   downstream then computes sum(L_i * w_i) itself -- shadow tiers, layer masks, fog and lighting
//   tails all keep their native math untouched. Reassembled with D3DAssemble, created via the live
//   device, wrapped in the minimal CGxShader layout the 0x4E flush reads (handle +0x20, created
//   +0x30). One patched shader per stock permutation, cached for the session.
//
//   Gates: compile toggle kAdtSplitHeightBlend; runtime WXL_ADT_HEIGHT_BLEND; the per-draw fast
//   path is one settings load + the resident-split-tile counter + the WDT MPHD 0x80 bit -- a
//   non-height map runs the untouched stock draw with ~zero added cost. Only ps_3_0, non-cube-env,
//   big-alpha (weighted-sum) permutations are patched; anything else falls back to the stock look.

#include "config.hpp"
#include "engine/hook/Hook.hpp"
#include "engine/hook/Registry.hpp"
#include "engine/events/Event.hpp"
#include "features/adtsplit/AdtSplit.hpp"
#include "features/adtsplit/HeightBlend.hpp"

#include "common/Config.hpp"
#include "common/Log.hpp"
#include "game/gx/Gx.hpp"
#include "offsets/engine/Gx.hpp"
#include "offsets/engine/Shader.hpp"
#include "offsets/game/ADT.hpp"

#include <windows.h>
#include <d3d9.h>
#include <d3dcompiler.h>

#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>

namespace
{
    namespace adt   = wxl::offsets::game::adt;
    namespace shoff = wxl::offsets::engine::shader;
    namespace gxoff = wxl::offsets::engine::gx;
    namespace ev    = wxl::events;
    namespace split = wxl::runtime::adtsplit;
    namespace hb    = wxl::features::heightblend;

    template <class T>
    inline T& At(void* base, size_t off) { return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + off); }

    // D3DAssemble has no SDK header declaration; both are resolved by name (grasswind precedent).
    typedef HRESULT(WINAPI* PFN_D3DAssemble)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*,
                                             UINT, ID3DBlob**, ID3DBlob**);
    typedef HRESULT(WINAPI* PFN_D3DDisassemble)(LPCVOID, SIZE_T, UINT, LPCSTR, ID3DBlob**);

    hb::Settings g_settings;
    bool         g_installed = false;

    adt::Map_SurfaceChunkDrawShaderFn g_origDraw           = nullptr;
    adt::Map_BuildTerrainConstantsFn  g_origBuildConstants = nullptr;

    // Patched-PS cache: stock CGxShader wrapper -> module wrapper (null = patch failed, draw stock).
    // Session-lifetime; entries are only ever read/written on the draw thread.
    std::unordered_map<void*, void*> g_patched;

    std::atomic<uint32_t> g_statPatched{ 0 }, g_statPatchFail{ 0 }, g_statChunks{ 0 };
    std::atomic<bool>     g_activeFlag{ false }; // last fast-path verdict, for the Lua readout

    HMODULE Compiler()
    {
        HMODULE d = GetModuleHandleA("d3dcompiler_47.dll");
        return d ? d : LoadLibraryA("d3dcompiler_47.dll");
    }

    // ---------------------------------------------------------------- disassembly surgery
    /// One parsed "texld rD, vC, sK" statement of the stock shader.
    struct TexldInfo
    {
        std::string dest;    // "r4"
        std::string coord;   // "v6" (kept verbatim, swizzles included)
        size_t      lineEnd; // offset of the line's trailing '\n' + 1 (insertion point)
        bool        found = false;
    };

    /// Trims ASCII whitespace from both ends.
    std::string Trim(const std::string& s)
    {
        size_t b = 0, e = s.size();
        while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
        while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
        return s.substr(b, e - b);
    }

    /// Highest r<N> temp register referenced anywhere in the text (comments included -- an
    /// overestimate only wastes registers, never collides).
    int MaxTempReg(const std::string& s)
    {
        int mx = -1;
        for (size_t i = 0; i + 1 < s.size(); ++i)
        {
            if (s[i] != 'r') continue;
            if (i > 0)
            {
                const unsigned char prev = static_cast<unsigned char>(s[i - 1]);
                if (std::isalnum(prev) || prev == '_') continue;
            }
            size_t j = i + 1;
            int v = 0;
            bool any = false;
            while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j])))
            {
                v = v * 10 + (s[j] - '0');
                ++j;
                any = true;
            }
            if (any && v > mx) mx = v;
        }
        return mx;
    }

    /**
     * @brief Finds the texld statement sampling s<sampler> and parses its dest/coord operands.
     *
     * Disassembly statements look like "    texld r2, v2, s0". The first match wins (layer and
     * alpha samplers are each sampled exactly once in the stock permutations).
     */
    TexldInfo FindTexld(const std::string& text, unsigned sampler)
    {
        TexldInfo out;
        char sTok[8];
        std::snprintf(sTok, sizeof sTok, "s%u", sampler);
        size_t pos = 0;
        while (pos < text.size())
        {
            size_t eol = text.find('\n', pos);
            if (eol == std::string::npos) eol = text.size();
            const std::string line = text.substr(pos, eol - pos);
            const std::string t = Trim(line);
            if (t.compare(0, 5, "texld") == 0)
            {
                // split on commas: "texld r2" | " v2" | " s0"
                const size_t c1 = t.find(',');
                const size_t c2 = c1 == std::string::npos ? std::string::npos : t.find(',', c1 + 1);
                if (c1 != std::string::npos && c2 != std::string::npos)
                {
                    const std::string samp = Trim(t.substr(c2 + 1));
                    if (samp == sTok)
                    {
                        const size_t sp = t.find_last_of(" \t", c1);
                        out.dest    = Trim(t.substr(sp == std::string::npos ? 5 : sp, c1 - (sp == std::string::npos ? 5 : sp)));
                        out.coord   = Trim(t.substr(c1 + 1, c2 - c1 - 1));
                        out.lineEnd = eol < text.size() ? eol + 1 : eol;
                        out.found   = !out.dest.empty() && out.dest[0] == 'r' &&
                                      !out.coord.empty() && out.coord[0] == 'v';
                        return out;
                    }
                }
            }
            pos = eol + 1;
        }
        return out;
    }

    /// Offset just past the line containing `needle` (0 when absent).
    size_t AfterLine(const std::string& text, const char* needle)
    {
        const size_t at = text.find(needle);
        if (at == std::string::npos) return 0;
        const size_t eol = text.find('\n', at);
        return eol == std::string::npos ? text.size() : eol + 1;
    }

    /// Offset just past the LAST "dcl"-prefixed statement line (0 when none).
    size_t AfterLastDcl(const std::string& text)
    {
        size_t pos = 0, best = 0;
        while (pos < text.size())
        {
            size_t eol = text.find('\n', pos);
            if (eol == std::string::npos) eol = text.size();
            const std::string t = Trim(text.substr(pos, eol - pos));
            if (t.compare(0, 3, "dcl") == 0)
                best = eol < text.size() ? eol + 1 : eol;
            pos = eol + 1;
        }
        return best;
    }

    /**
     * @brief Patches one stock terrain PS permutation's disassembly with the height-blend weights.
     *
     * Preconditions established by the caller: ps_3_0, no cube sampler, big-alpha (weighted-sum)
     * family. n = layer count (2..4). Returns the full modified source, or empty on any parse miss
     * (caller degrades to the stock shader).
     */
    std::string InjectHeightBlend(const std::string& text, uint32_t n)
    {
        TexldInfo layer[4];
        for (uint32_t i = 0; i < n; ++i)
        {
            layer[i] = FindTexld(text, i);
            if (!layer[i].found) return std::string();
        }
        const TexldInfo alpha = FindTexld(text, n);
        if (!alpha.found) return std::string();

        const size_t defAt = AfterLine(text, "ps_3_0");
        const size_t dclAt = AfterLastDcl(text);
        if (defAt == 0 || dclAt == 0 || alpha.lineEnd <= dclAt) return std::string();

        const int maxTemp = MaxTempReg(text);
        if (maxTemp < 0 || maxTemp + 6 > 31) return std::string(); // need 6 free ps_3_0 temps
        const int T0 = maxTemp + 1; // T0..T3 height samples, T4 = h vector, T5 = scratch
        const int T4 = maxTemp + 5;
        const int T5 = maxTemp + 6;

        const char ch = g_settings.channelRed ? 'x' : 'w'; // _h height channel: .r or .a
        const char* comp[4] = { "x", "y", "z", "w" };
        const std::string& A = alpha.dest;

        char b[160];
        std::string blend;
        for (uint32_t i = 0; i < n; ++i)
        {
            std::snprintf(b, sizeof b, "    texld r%d, %s, s%u\n", T0 + static_cast<int>(i),
                          layer[i].coord.c_str(), 9u + i);
            blend += b;
        }
        std::snprintf(b, sizeof b, "    mov r%d, c30.y\n", T4);                       blend += b;
        for (uint32_t i = 0; i < n; ++i)
        {
            std::snprintf(b, sizeof b, "    mov r%d.%s, r%d.%c\n", T4, comp[i],
                          T0 + static_cast<int>(i), ch);
            blend += b;
        }
        // base weights: (1 - sat(a1+a2+a3), a1, a2, a3)
        std::snprintf(b, sizeof b, "    dp3_sat r%d.x, %s, c30.x\n", T5, A.c_str());  blend += b;
        std::snprintf(b, sizeof b, "    add r%d.x, c30.x, -r%d.x\n", T5, T5);         blend += b;
        std::snprintf(b, sizeof b, "    mov r%d.yzw, %s.xxyz\n", T5, A.c_str());      blend += b;
        // w = base * (h * scale + offset). D3D9 asm allows at most ONE unique c# per instruction
        // (X5584), so the offset vector is staged through a temp: rT0's height sample was already
        // consumed into the h vector above, making it free scratch here.
        std::snprintf(b, sizeof b, "    mov r%d, c23\n", T0);                         blend += b;
        std::snprintf(b, sizeof b, "    mad r%d, r%d, c22, r%d\n", T4, T4, T0);       blend += b;
        std::snprintf(b, sizeof b, "    mul r%d, r%d, r%d\n", T4, T4, T5);            blend += b;
        // sharpen: w *= 1 - sat((max4(w) - w) * sharpness)
        std::snprintf(b, sizeof b, "    max r%d.x, r%d.x, r%d.y\n", T5, T4, T4);      blend += b;
        std::snprintf(b, sizeof b, "    max r%d.x, r%d.x, r%d.z\n", T5, T5, T4);      blend += b;
        std::snprintf(b, sizeof b, "    max r%d.x, r%d.x, r%d.w\n", T5, T5, T4);      blend += b;
        std::snprintf(b, sizeof b, "    add r%d, r%d.x, -r%d\n", T5, T5, T4);         blend += b;
        std::snprintf(b, sizeof b, "    mul_sat r%d, r%d, c24.x\n", T5, T5);          blend += b;
        std::snprintf(b, sizeof b, "    add r%d, c30.x, -r%d\n", T5, T5);             blend += b;
        std::snprintf(b, sizeof b, "    mul r%d, r%d, r%d\n", T4, T4, T5);            blend += b;
        // renormalize: w /= (dot(w, 1) + eps)
        std::snprintf(b, sizeof b, "    dp4 r%d.x, r%d, c30.x\n", T5, T4);            blend += b;
        std::snprintf(b, sizeof b, "    add r%d.x, r%d.x, c30.z\n", T5, T5);          blend += b;
        std::snprintf(b, sizeof b, "    rcp r%d.x, r%d.x\n", T5, T5);                 blend += b;
        std::snprintf(b, sizeof b, "    mul r%d, r%d, r%d.x\n", T4, T4, T5);          blend += b;
        // write w1..w3 back into the alpha register (.a shadow preserved); the stock math downstream
        // recomputes layer0's weight as 1 - (w1+w2+w3) = w0 and mixes sum(L_i * w_i) itself.
        std::snprintf(b, sizeof b, "    mov %s.xyz, r%d.yzww\n", A.c_str(), T4);      blend += b;

        std::string dcls;
        for (uint32_t i = 0; i < n; ++i)
        {
            std::snprintf(b, sizeof b, "    dcl_2d s%u\n", 9u + i);
            dcls += b;
        }

        // Insert back-to-front (descending offsets) so earlier offsets stay valid whatever order
        // the disassembler emitted the def/dcl prologue in.
        struct Ins { size_t at; const std::string* what; };
        const std::string defLine = "    def c30, 1.0, 0.0, 0.0001, 0.0\n";
        Ins ins[3] = { { alpha.lineEnd, &blend }, { dclAt, &dcls }, { defAt, &defLine } };
        for (int i = 0; i < 2; ++i)      // 3-element sort, descending by offset
            for (int j = i + 1; j < 3; ++j)
                if (ins[j].at > ins[i].at) { const Ins t2 = ins[i]; ins[i] = ins[j]; ins[j] = t2; }
        std::string out = text;
        for (const Ins& e : ins)
            out.insert(e.at, *e.what);
        return out;
    }

    // ---------------------------------------------------------------- shader build + wrapper mint
    /**
     * @brief Wraps freshly created device bytecode in the minimal CGxShader layout the GxRs 0x4E
     *        flush reads: live handle +0x20, created flag +0x30 (so the flush binds the handle and
     *        never re-creates), bytecode ptr/len +0x50/+0x4C kept valid for any re-create path.
     */
    void* MakeWrapper(const void* bytecode, uint32_t len)
    {
        auto* dev = static_cast<IDirect3DDevice9*>(wxl::game::gx::RawDevice());
        if (!dev) return nullptr;
        IDirect3DPixelShader9* ps = nullptr;
        if (FAILED(dev->CreatePixelShader(static_cast<const DWORD*>(bytecode), &ps)) || !ps)
            return nullptr;
        auto* copy = new uint8_t[len];
        std::memcpy(copy, bytecode, len);
        auto* w = new uint8_t[shoff::kCgxShaderWrapBytes]();
        At<void*>(w, shoff::kCgxShaderHandle)          = ps;
        At<uint32_t>(w, shoff::kCgxShaderCreated)      = 1;
        At<uint32_t>(w, shoff::kCgxShaderByteLen)      = len;
        At<const void*>(w, shoff::kCgxShaderBytePtr)   = copy;
        return w;
    }

    /**
     * @brief Builds the height-blend replacement for one stock permutation wrapper (null = fail).
     */
    void* BuildPatched(void* stock, uint32_t n)
    {
        const uint8_t* code = At<const uint8_t*>(stock, shoff::kCgxShaderBytePtr);
        const uint32_t len  = At<uint32_t>(stock, shoff::kCgxShaderByteLen);
        if (!code || len < 8 || len > 0x20000) return nullptr;
        uint32_t ver = 0;
        std::memcpy(&ver, code, 4);
        if (ver != 0xFFFF0300u) // ps_3_0 only (ps_2_0 shadowed perms overflow the ALU budget)
        {
            WLOG_WARN("height-blend: stock terrain PS (n=%u) is not ps_3_0 (0x%08X); drawing stock", n, ver);
            return nullptr;
        }

        HMODULE comp = Compiler();
        auto disasm   = reinterpret_cast<PFN_D3DDisassemble>(comp ? GetProcAddress(comp, "D3DDisassemble") : nullptr);
        auto assemble = reinterpret_cast<PFN_D3DAssemble>(comp ? GetProcAddress(comp, "D3DAssemble") : nullptr);
        if (!disasm || !assemble)
        {
            WLOG_WARN("height-blend: d3dcompiler_47 D3DDisassemble/D3DAssemble unavailable");
            return nullptr;
        }

        ID3DBlob* textBlob = nullptr;
        if (FAILED(disasm(code, len, 0, nullptr, &textBlob)) || !textBlob)
            return nullptr;
        std::string text(static_cast<const char*>(textBlob->GetBufferPointer()));
        textBlob->Release();

        if (text.find("ps_3_0") == std::string::npos) return nullptr;
        if (text.find("dcl_cube") != std::string::npos) return nullptr; // env-layer0 permutation
        if (text.find("dp3") == std::string::npos)                       // not the weighted-sum family
        {
            WLOG_WARN("height-blend: stock PS (n=%u, %u B) is the classic lrp family; drawing stock", n, len);
            return nullptr;
        }

        const std::string patched = InjectHeightBlend(text, n);
        if (patched.empty())
        {
            WLOG_WARN("height-blend: injection parse failed (n=%u, %u B); drawing stock", n, len);
            return nullptr;
        }

        ID3DBlob* blob = nullptr;
        ID3DBlob* err  = nullptr;
        const HRESULT hr = assemble(patched.c_str(), patched.size(), "wxlHeightBlend", nullptr, nullptr, 0,
                                    &blob, &err);
        if (FAILED(hr) || !blob)
        {
            WLOG_WARN("height-blend: reassemble failed (n=%u): %s", n,
                      err ? static_cast<const char*>(err->GetBufferPointer()) : "?");
            if (err) err->Release();
            if (blob) blob->Release();
            return nullptr;
        }
        if (err) err->Release();

        void* wrapper = MakeWrapper(blob->GetBufferPointer(), static_cast<uint32_t>(blob->GetBufferSize()));
        const uint32_t outLen = static_cast<uint32_t>(blob->GetBufferSize());
        blob->Release();
        if (!wrapper) return nullptr;

        WLOG_INFO("height-blend: patched terrain PS (n=%u, %u -> %u B)", n, len, outLen);
        ev::AdtHeightBlendArgs a{ n, len, outLen };
        ev::Emit(ev::Event::OnAdtHeightBlend, &a);
        return wrapper;
    }

    /// Cached build (one attempt per stock permutation per session; null entries mean "draw stock").
    void* GetPatched(void* stock, uint32_t n)
    {
        auto it = g_patched.find(stock);
        if (it != g_patched.end()) return it->second;
        void* built = BuildPatched(stock, n);
        g_patched.emplace(stock, built);
        if (built) g_statPatched.fetch_add(1, std::memory_order_relaxed);
        else       g_statPatchFail.fetch_add(1, std::memory_order_relaxed);
        return built;
    }

    // ---------------------------------------------------------------- the draw-leaf detour
    /// Fast-path gate: runtime toggle + any split tile resident + WDT MPHD height-texturing (0x80).
    inline bool Active()
    {
        if (!g_settings.enabled) return false;
        if (split::ResidentTilesRelaxed() == 0) return false;
        if ((*reinterpret_cast<const uint32_t*>(adt::kMphdFlags) & 0x80u) == 0) return false;
        return true;
    }

    /**
     * @brief Detours the shader-path per-chunk terrain draw (0x007D2D70).
     *
     * Non-height maps: three cheap loads, then the untouched original. Height chunks: resolve the
     * per-layer "_h" data through the split reader, bind heights at s9..s12, upload c22..c24, swap
     * GxRs 0x4E to the patched wrapper, run the original (its single DIP flushes our state), then
     * restore the stock wrapper and clear the height stages.
     */
    void __fastcall hkSurfaceChunkDrawShader(void* node, void* edx)
    {
        const bool active = Active();
        g_activeFlag.store(active, std::memory_order_relaxed);
        if (!active) { g_origDraw(node, edx); return; }

        const uint32_t n     = At<uint8_t>(node, adt::kOffChunkNodeLayerCount);
        const uint16_t flags = At<uint16_t>(node, adt::kOffChunkNodeFlags);
        void* mapChunk       = At<void*>(node, adt::kOffChunkNodeChunk);
        if (n < 2 || n > 4 || (flags & 0x4u) != 0 || !mapChunk) // 1 layer / cube-env: nothing to blend
        {
            g_origDraw(node, edx);
            return;
        }

        // chunk -> owning tile area, the engine's own link arithmetic (same as the split fill seam)
        void* area = nullptr;
        const uint32_t link = At<uint32_t>(mapChunk, adt::kOffChunkTexOwnerSrc);
        if (link != 0 && (link & 1u) == 0)
            area = *reinterpret_cast<void**>(static_cast<uintptr_t>(link) + 8);
        if (!area) { g_origDraw(node, edx); return; }

        split::HeightLayer hl[4] = {};
        bool ok = true;
        for (uint32_t i = 0; i < n && ok; ++i)
        {
            const uint32_t texId = At<uint32_t>(node, adt::kOffChunkLayerRecords +
                                                          i * adt::kChunkLayerRecordStride +
                                                          adt::kOffLayerSlotTexId);
            ok = split::GetHeightLayer(area, texId, hl[i]);
        }
        if (!ok) { g_origDraw(node, edx); return; } // not a height-textured split tile

        void** psTable = reinterpret_cast<void**>(adt::kActiveTerrainPs);
        void* stock    = psTable[n - 1];
        void* gxDev    = *reinterpret_cast<void**>(gxoff::kGxDevicePtr);
        if (!stock || !gxDev) { g_origDraw(node, edx); return; }

        void* patched = GetPatched(stock, n);
        if (!patched) { g_origDraw(node, edx); return; }

        auto rsSet      = reinterpret_cast<adt::Map_SamplerBindFn>(adt::kSetSamplerTexture);
        auto texResolve = reinterpret_cast<adt::Map_TexResolveFn>(adt::kTexResolve);

        // c22 = heightScale.xyzw, c23 = heightOffset.xyzw, c24 = (sharpness, 0, 0, 0). Layers whose
        // height texture is unavailable degrade to (0, 1) = the Legion solid-white identity; absent
        // layers get (0, 0) so their weight is exactly zero whatever the sampler returns.
        float c[3][4] = {};
        for (uint32_t i = 0; i < n; ++i)
        {
            void* gxTex = hl[i].texture ? texResolve(hl[i].texture, 0, 0) : nullptr;
            if (gxTex)
            {
                c[0][i] = hl[i].heightScale;
                c[1][i] = hl[i].heightOffset;
                rsSet(gxDev, nullptr, adt::kSamplerSelHeight0 + i, gxTex);
            }
            else
            {
                c[0][i] = 0.0f;
                c[1][i] = 1.0f;
            }
        }
        c[2][0] = g_settings.sharpness;
        reinterpret_cast<shoff::ShaderConstantsSetHelperFn>(shoff::kShaderConstantsSet)(
            4, static_cast<int>(adt::kPsConstTerrainBindBase), &c[0][0], 3);

        rsSet(gxDev, nullptr, adt::kGxStatePixelShader, patched);
        g_origDraw(node, edx); // untouched original: VS pick, diffuse/alpha binds, single DIP
        rsSet(gxDev, nullptr, adt::kGxStatePixelShader, stock); // the rest of the bucket stays stock
        for (uint32_t i = 0; i < n; ++i)
            rsSet(gxDev, nullptr, adt::kSamplerSelHeight0 + i, nullptr);

        g_statChunks.fetch_add(1, std::memory_order_relaxed);
    }

    /**
     * @brief Post-hook on CMapRenderChunk::SetVertexShader (0x007D0050), the per-chunk VS constant
     *        build: applies the MTXP UV-tiling exponent (flags bits 4..7) by dividing each drawn
     *        layer's c18+i tiling vec4 .xy by (1 << exp) and re-uploading c18..c(18+n-1) -- exactly
     *        Legion's uv = base / (1 << exp) (FUN_00c9e1ef). The height texlds reuse the layer
     *        texcoords, so they inherit the corrected tiling for free. Runs on height-textured
     *        split maps only (same fast path as the draw detour); the original always runs first
     *        and rebuilds the whole block per chunk, so the divide never accumulates.
     */
    void __cdecl hkBuildTerrainConstants(void* node, uint32_t a1, uint32_t a2)
    {
        g_origBuildConstants(node, a1, a2);
        if (!Active()) return;

        const uint32_t n = At<uint8_t>(node, adt::kOffChunkNodeLayerCount);
        void* mapChunk   = At<void*>(node, adt::kOffChunkNodeChunk);
        if (n == 0 || n > 4 || !mapChunk) return;
        const uint32_t link = At<uint32_t>(mapChunk, adt::kOffChunkTexOwnerSrc);
        if (link == 0 || (link & 1u) != 0) return;
        void* area = *reinterpret_cast<void**>(static_cast<uintptr_t>(link) + 8);
        if (!area) return;

        float* c18 = reinterpret_cast<float*>(adt::kVsConstC18);
        bool any = false;
        for (uint32_t i = 0; i < n; ++i)
        {
            const uint32_t texId = At<uint32_t>(node, adt::kOffChunkLayerRecords +
                                                          i * adt::kChunkLayerRecordStride +
                                                          adt::kOffLayerSlotTexId);
            split::HeightLayer hl{};
            if (!split::GetHeightLayer(area, texId, hl)) return; // not a height tile: leave stock
            if (hl.tilingExp == 0) continue;
            const float div = static_cast<float>(1u << (hl.tilingExp & 0xFu));
            c18[i * 4 + 0] /= div;
            c18[i * 4 + 1] /= div;
            any = true;
        }
        if (any)
            reinterpret_cast<shoff::ShaderConstantsSetHelperFn>(shoff::kShaderConstantsSet)(
                0, static_cast<int>(adt::kVsConstC18Reg), c18, static_cast<int>(n));
    }

    // ---------------------------------------------------------------- install
    bool InstallHeightBlend()
    {
        char buf[64];
        if (wxl::config::Raw("WXL_ADT_HEIGHT_BLEND", buf, sizeof buf))
            g_settings.enabled = wxl::config::Truthy(buf, true);
        if (wxl::config::Raw("WXL_ADT_HEIGHT_SHARPNESS", buf, sizeof buf))
        {
            const float v = static_cast<float>(std::atof(buf));
            if (v >= 0.0f && v <= 16.0f) g_settings.sharpness = v;
        }
        if (wxl::config::Raw("WXL_ADT_HEIGHT_CHANNEL", buf, sizeof buf))
            g_settings.channelRed = (buf[0] == 'r' || buf[0] == 'R');

        g_installed = wxl::hook::Install("AdtHeightBlend.SurfaceChunkDrawShader",
                                         adt::kSurfaceChunkDrawShader,
                                         &hkSurfaceChunkDrawShader, &g_origDraw);
        const bool uv = wxl::hook::Install("AdtHeightBlend.BuildTerrainConstants",
                                           adt::kBuildTerrainConstants,
                                           &hkBuildTerrainConstants, &g_origBuildConstants);
        if (!uv)
            WLOG_WARN("height-blend: UV-tiling constants hook failed; MTXP tiling exponents ignored");
        if (g_installed)
            WLOG_INFO("height-blend: terrain draw detour installed (enabled=%d sharpness=%.2f channel=%c)",
                      g_settings.enabled ? 1 : 0, g_settings.sharpness,
                      g_settings.channelRed ? 'r' : 'a');
        else
            WLOG_WARN("height-blend: draw detour install failed; terrain renders stock");
        return true; // non-fatal
    }
}

namespace wxl::features::heightblend
{
    Settings& Get() { return g_settings; }

    Stats GetStats()
    {
        Stats s{};
        s.patchedShaders = g_statPatched.load(std::memory_order_relaxed);
        s.patchFailures  = g_statPatchFail.load(std::memory_order_relaxed);
        s.chunksDrawn    = g_statChunks.load(std::memory_order_relaxed);
        s.active         = g_activeFlag.load(std::memory_order_relaxed);
        return s;
    }

    bool Installed() { return g_installed; }

    void InvalidateShaders()
    {
        // Entries are dropped, not freed: the GxState 0x4E cache may still reference a wrapper this
        // frame, and the handful of shader objects per session is not worth a deferred-free scheme.
        g_patched.clear();
    }
}

WXL_REGISTER_FEATURE("adt-height-blend", wxl::features::kAdtSplitHeightBlend, InstallHeightBlend)
