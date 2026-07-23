// Modern-WMO Composite fix: composite the second layer by the texture's own alpha, in-memory PS patch.
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
//
// MECHANISM
//   The stock two-layer WMO pixel shader (MapObjComposite, effect id 6) does, per pixel:
//       texld r0, t0, s0            ; r0 = base   (texture_1)
//       texld r1, t1, s1            ; r1 = overlay(texture_2)
//       lrp   r2, v1.w, r0, r1      ; r2 = lerp(r1, r0, v1.w)  -- blend by SECONDARY vertex-colour alpha
//   A modern material's texture_2 is an alpha-masked detail overlay (measured 62-92% transparent), so
//   this blend mixes in the overlay's dark body and renders its highlight lines as dark stripes.
//   The fix rewrites that ONE instruction to composite by the overlay's own alpha:
//       lrp   r2, r1.w, r1, r0      ; r2 = lerp(r0, r1, r1.w) = lerp(base, overlay, overlay.a)
//   at r1.w = 0 (transparent) the base shows; at r1.w = 1 (a highlight texel) the overlay shows. The
//   patch is applied IN MEMORY to a copy of the stock shader (disassemble -> rewrite -> reassemble); no
//   .bls file is produced. It is scoped to MODERN WMOs -- stock two-layer content still wants the
//   vertex-alpha blend -- via two frame-stash hooks on the WMO batch-draw leaves plus a post-hook on the
//   effect bind that swaps GxState slot 0x4E only when the current root is modern and the active effect
//   collection is the Composite one.

#include "config.hpp"
#include "engine/hook/Hook.hpp"
#include "engine/hook/Registry.hpp"
#include "features/wmonative/CompositeShader.hpp"
#include "features/wmonative/WmoNative.hpp"

#include "common/Log.hpp"
#include "game/gx/Gx.hpp"
#include "offsets/engine/Gx.hpp"
#include "offsets/engine/Shader.hpp"
#include "offsets/game/WMO.hpp"

#include <windows.h>
#include <d3d9.h>
#include <d3dcompiler.h>

#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>

namespace
{
    namespace shoff = wxl::offsets::engine::shader;
    namespace gxoff = wxl::offsets::engine::gx;
    namespace wmo   = wxl::offsets::game::wmo;

    template <class T>
    inline T& At(void* base, size_t off) { return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + off); }

    // D3DAssemble has no SDK header declaration; both are resolved by name (height-blend precedent).
    typedef HRESULT(WINAPI* PFN_D3DAssemble)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*,
                                             UINT, ID3DBlob**, ID3DBlob**);
    typedef HRESULT(WINAPI* PFN_D3DDisassemble)(LPCVOID, SIZE_T, UINT, LPCSTR, ID3DBlob**);

    // GxState setter, same address/convention height-blend uses to swap the pixel-shader slot.
    using GxSetFn = void(__fastcall*)(void* device, void* edx, uint32_t selector, void* value);

    // ------------------------------------------------------------------ state
    std::atomic<bool>     g_fixEnabled{ true };
    bool                  g_installed = false;

    wmo::Wmo_RenderLeafFn g_origExtRender = nullptr;
    wmo::Wmo_RenderLeafFn g_origIntRender = nullptr;
    shoff::EffectBindFn   g_origEffectBind = nullptr;

    // Set true for the duration of a MODERN WMO batch-draw leaf; read in the effect-bind post-hook.
    // Render is single-threaded (12340 has no MT render), so a plain bool is sufficient.
    bool                  g_curModern = false;
    // The current group is in the two-UV vertex format (its VB carries BOTH UV sets per vertex), the
    // precondition for routing a single-layer batch onto set1. Captured in the frame-stash from the group.
    bool                  g_curTwoUv = false;
    std::atomic<bool>     g_vsRouteEnabled{ true }; // live A/B of the single-layer set1 VS route

    // Composite base/overlay texcoord swap, default ON. The walker reverses a 2-set group's two MOTV
    // chunks at load so single-layer effects (which sample only texcoord0) get the group's LAST UV set --
    // Legion's "a material samples the last N UV sets" rule. A two-layer material samples both, so its
    // Composite shader must UNDO that reversal (base <- overlay texcoord, overlay <- base texcoord),
    // landing the base back on MOTV[0] and the overlay on MOTV[1]. A Composite material is always in a
    // 2+-set group, so this always pairs with a reversed group; 0 disables it for A/B. Cached per variant.
    std::atomic<int>      g_baseUvSet{0};

    // Stock Composite PS wrapper -> patched wrapper, one cache per base-UV variant (null = draw stock).
    std::unordered_map<void*, void*> g_patched[2];
    // Stock single-layer VS wrapper -> a copy that outputs its texcoord from the SET1 input (null = fail).
    std::unordered_map<void*, void*> g_vsPatched;

    std::atomic<uint32_t> g_statPatched{ 0 }, g_statPatchFail{ 0 }, g_statBatches{ 0 };
    std::atomic<uint32_t> g_statVsPatched{ 0 }, g_statVsSwapped{ 0 };

    HMODULE Compiler()
    {
        HMODULE d = GetModuleHandleA("d3dcompiler_47.dll");
        return d ? d : LoadLibraryA("d3dcompiler_47.dll");
    }

    // ------------------------------------------------------------------ disassembly surgery
    /// One parsed `texld rDest, coord, s<sampler>` statement.
    struct Texld { std::string dest, coord; bool found = false; };

    /// Finds the first `texld rDest, coord, s<sampler>` and returns its dest + coord operands.
    Texld FindTexld(const std::string& text, unsigned sampler)
    {
        Texld out;
        char sTok[8];
        std::snprintf(sTok, sizeof sTok, "s%u", sampler);
        auto trim = [](std::string s) {
            size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
            return a == std::string::npos ? std::string{} : s.substr(a, b - a + 1);
        };
        size_t pos = 0;
        while (pos < text.size())
        {
            size_t eol = text.find('\n', pos);
            if (eol == std::string::npos) eol = text.size();
            const std::string line = text.substr(pos, eol - pos);
            const size_t t = line.find("texld");
            if (t != std::string::npos)
            {
                const size_t c1 = line.find(',', t);
                const size_t c2 = c1 == std::string::npos ? std::string::npos : line.find(',', c1 + 1);
                if (c1 != std::string::npos && c2 != std::string::npos && trim(line.substr(c2 + 1)) == sTok)
                {
                    out.dest  = trim(line.substr(t + 5, c1 - (t + 5)));
                    out.coord = trim(line.substr(c1 + 1, c2 - (c1 + 1)));
                    out.found = !out.dest.empty() && !out.coord.empty();
                    return out;
                }
            }
            pos = eol + 1;
        }
        return out;
    }

    /**
     * @brief Rewrites the Composite pixel shader: (1) composite the second layer by the OVERLAY texture's
     *        own alpha, and (2) optionally sample the BASE texture from the second UV set.
     *
     * (1) finds the stock operand triplet `v1.w, <base>, <overlay>` (the lrp factor = secondary vertex
     *     alpha, then the two texld results) and rewrites it to `<overlay>.w, <overlay>, <base>` -- flipping
     *     the lerp direction and sourcing the factor from the overlay's alpha.
     * (2) when baseUvSet == 1, rewrites the base texld's coordinate operand to the overlay's coordinate
     *     (the second texcoord the VS already emits), so the base samples UV set 1 -- the modern shaders'
     *     per-texture texcoord routing that collapsing to effect 6 discarded.
     * Returns "" if the shape does not match (any non-Composite permutation is drawn stock).
     */
    std::string InjectComposite(const std::string& text, int baseUvSet)
    {
        const Texld base    = FindTexld(text, 0);
        const Texld overlay = FindTexld(text, 1);
        if (!base.found || !overlay.found) return {};

        const std::string stock = "v1.w, " + base.dest + ", " + overlay.dest;
        const size_t at = text.find(stock);
        if (at == std::string::npos) return {};
        size_t lineStart = text.rfind('\n', at);
        lineStart = (lineStart == std::string::npos) ? 0 : lineStart + 1;
        if (text.find("lrp", lineStart) == std::string::npos || text.find("lrp", lineStart) > at)
            return {};

        std::string out = text;
        out.replace(at, stock.size(), overlay.dest + ".w, " + overlay.dest + ", " + base.dest);

        if (baseUvSet == 1 && base.coord != overlay.coord)
        {
            // Re-source the BASE texld from the overlay's texcoord (the group's other UV set). The walker
            // reverses the two MOTV chunks of a 2-set group at load so single-layer effects (texcoord0
            // only) get the group's last set; the two-layer Composite base then re-samples texcoord1 to
            // land back on MOTV[0]. The overlay is intentionally left as-is (base-only swap) -- this is the
            // combination measured working in game.
            const std::string bFrom = "texld " + base.dest + ", " + base.coord    + ", s0";
            const std::string bTo   = "texld " + base.dest + ", " + overlay.coord + ", s0";
            const size_t bp = out.find(bFrom);
            if (bp != std::string::npos) out.replace(bp, bFrom.size(), bTo);
        }
        return out;
    }

    // ------------------------------------------------------------------ wrapper mint (height-blend layout)
    /// Wraps freshly created device bytecode in the minimal CGxShader layout the 0x4E flush reads:
    /// live handle +0x20, created flag +0x30, bytecode ptr/len +0x50/+0x4C.
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
        At<void*>(w, shoff::kCgxShaderHandle)        = ps;
        At<uint32_t>(w, shoff::kCgxShaderCreated)    = 1;
        At<uint32_t>(w, shoff::kCgxShaderByteLen)    = len;
        At<const void*>(w, shoff::kCgxShaderBytePtr) = copy;
        return w;
    }

    /// Builds the Composite replacement for one stock wrapper + base-UV variant (null = fail / not Composite).
    void* BuildPatched(void* stock, int baseUvSet)
    {
        const uint8_t* code = At<const uint8_t*>(stock, shoff::kCgxShaderBytePtr);
        const uint32_t len  = At<uint32_t>(stock, shoff::kCgxShaderByteLen);
        if (!code || len < 8 || len > 0x20000) return nullptr;
        uint32_t ver = 0;
        std::memcpy(&ver, code, 4);
        if (ver != 0xFFFF0200u && ver != 0xFFFF0300u) return nullptr; // ps_2_0 / ps_3_0 only

        HMODULE comp = Compiler();
        auto disasm   = reinterpret_cast<PFN_D3DDisassemble>(comp ? GetProcAddress(comp, "D3DDisassemble") : nullptr);
        auto assemble = reinterpret_cast<PFN_D3DAssemble>(comp ? GetProcAddress(comp, "D3DAssemble") : nullptr);
        if (!disasm || !assemble) return nullptr;

        ID3DBlob* textBlob = nullptr;
        if (FAILED(disasm(code, len, 0, nullptr, &textBlob)) || !textBlob) return nullptr;
        std::string text(static_cast<const char*>(textBlob->GetBufferPointer()));
        textBlob->Release();

        const std::string patched = InjectComposite(text, baseUvSet);
        if (patched.empty()) return nullptr; // not the two-texture Composite shape -> draw stock

        ID3DBlob* blob = nullptr;
        ID3DBlob* err  = nullptr;
        const HRESULT hr = assemble(patched.c_str(), patched.size(), "wxlComposite", nullptr, nullptr, 0,
                                    &blob, &err);
        if (FAILED(hr) || !blob)
        {
            WLOG_WARN("wmo-composite: reassemble failed: %s",
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

        WLOG_INFO("wmo-composite: patched Composite PS (%u -> %u B, baseUv=%d)", len, outLen, baseUvSet);
        return wrapper;
    }

    /// Cached build per (stock permutation, base-UV variant); null means "draw stock".
    void* GetPatched(void* stock, int baseUvSet)
    {
        auto& cache = g_patched[baseUvSet & 1];
        auto it = cache.find(stock);
        if (it != cache.end()) return it->second;
        void* built = BuildPatched(stock, baseUvSet & 1);
        cache.emplace(stock, built);
        if (built) g_statPatched.fetch_add(1, std::memory_order_relaxed);
        else       g_statPatchFail.fetch_add(1, std::memory_order_relaxed);
        return built;
    }

    // ------------------------------------------------------------------ single-layer VS route (set1)
    /// Wraps freshly created VERTEX-shader bytecode in the minimal CGxShader layout the 0x4D flush reads.
    void* MakeVsWrapper(const void* bytecode, uint32_t len)
    {
        auto* dev = static_cast<IDirect3DDevice9*>(wxl::game::gx::RawDevice());
        if (!dev) return nullptr;
        IDirect3DVertexShader9* vs = nullptr;
        if (FAILED(dev->CreateVertexShader(static_cast<const DWORD*>(bytecode), &vs)) || !vs)
            return nullptr;
        auto* copy = new uint8_t[len];
        std::memcpy(copy, bytecode, len);
        auto* w = new uint8_t[shoff::kCgxShaderWrapBytes]();
        At<void*>(w, shoff::kCgxShaderHandle)        = vs;
        At<uint32_t>(w, shoff::kCgxShaderCreated)    = 1;
        At<uint32_t>(w, shoff::kCgxShaderByteLen)    = len;
        At<const void*>(w, shoff::kCgxShaderBytePtr) = copy;
        return w;
    }

    /**
     * @brief Rewrites a single-layer WMO vertex shader to output its texcoord from UV SET 1 instead of set 0.
     *
     * In a two-UV-format group the vertex carries both UV sets, exposed to the vertex declaration as
     * TEXCOORD0 and TEXCOORD1. A single-layer VS normally copies the set-0 input to its one texcoord output
     * (`mov o<k>.xy, v<a>` after `dcl_texcoord v<a>` / `dcl_texcoord o<k>.xy`). We re-source that copy from
     * the set-1 input: reuse an existing `dcl_texcoord1 v<b>` if the VS already declares it, else declare a
     * fresh input register for TEXCOORD1 and read that. Legion's rule -- a single-layer material samples the
     * group's LAST UV set -- then holds without touching the shared vertex buffer. "" = shape not matched.
     */
    std::string InjectSingleLayerSet1(const std::string& text)
    {
        // The texcoord OUTPUT register, from its declaration `dcl_texcoord o<k>.xy` (usage index 0).
        const size_t od = text.find("dcl_texcoord o");
        if (od == std::string::npos) return {};
        size_t oe = od + std::strlen("dcl_texcoord o");
        std::string outReg = "o";
        while (oe < text.size() && std::isdigit(static_cast<unsigned char>(text[oe]))) outReg += text[oe++];
        if (outReg.size() < 2) return {};

        // The set-1 INPUT register: an existing `dcl_texcoord1 v<b>`, else pick a fresh v<n> and declare it.
        std::string set1In;
        std::string extraDecl;
        const size_t i1 = text.find("dcl_texcoord1 v");
        if (i1 != std::string::npos)
        {
            size_t ie = i1 + std::strlen("dcl_texcoord1 ");
            set1In = "v";
            ++ie; // skip the 'v'
            while (ie < text.size() && std::isdigit(static_cast<unsigned char>(text[ie]))) set1In += text[ie++];
        }
        else
        {
            // Pick a free input register: one past the highest v<n> the shader already uses.
            int maxV = -1;
            for (size_t p = 0; (p = text.find('v', p)) != std::string::npos; ++p)
            {
                if (p && (std::isalnum(static_cast<unsigned char>(text[p - 1])) || text[p - 1] == '_')) continue;
                size_t q = p + 1; int v = 0; bool any = false;
                while (q < text.size() && std::isdigit(static_cast<unsigned char>(text[q]))) { v = v * 10 + (text[q++] - '0'); any = true; }
                if (any && v > maxV) maxV = v;
            }
            set1In = "v" + std::to_string(maxV + 1);
            extraDecl = "dcl_texcoord1 " + set1In + "\n";
        }
        if (set1In.size() < 2) return {};

        // Find the copy that writes the texcoord output and re-source it from the set-1 input.
        const size_t mv = text.find("mov " + outReg + ".xy,");
        if (mv == std::string::npos) return {};
        const size_t comma = text.find(',', mv);
        size_t eol = text.find('\n', mv);
        if (comma == std::string::npos || eol == std::string::npos) return {};

        std::string out = text;
        out.replace(comma + 1, eol - (comma + 1), " " + set1In);
        if (!extraDecl.empty())
        {
            // Insert the new input declaration next to the existing dcl_texcoord line (keeps dcls grouped).
            const size_t at = out.find("dcl_texcoord o");
            const size_t ls = out.rfind('\n', at);
            const size_t ins = (ls == std::string::npos) ? 0 : ls + 1;
            out.insert(ins, extraDecl);
        }
        return out;
    }

    /// Builds the set1-routed replacement for one stock single-layer VS wrapper (null = fail).
    void* BuildPatchedVs(void* stock)
    {
        const uint8_t* code = At<const uint8_t*>(stock, shoff::kCgxShaderBytePtr);
        const uint32_t len  = At<uint32_t>(stock, shoff::kCgxShaderByteLen);
        if (!code || len < 8 || len > 0x20000) return nullptr;
        uint32_t ver = 0;
        std::memcpy(&ver, code, 4);
        if ((ver & 0xFFFF0000u) != 0xFFFE0000u) return nullptr; // a vertex shader (vs_x_x) only

        HMODULE comp = Compiler();
        auto disasm   = reinterpret_cast<PFN_D3DDisassemble>(comp ? GetProcAddress(comp, "D3DDisassemble") : nullptr);
        auto assemble = reinterpret_cast<PFN_D3DAssemble>(comp ? GetProcAddress(comp, "D3DAssemble") : nullptr);
        if (!disasm || !assemble) return nullptr;

        ID3DBlob* textBlob = nullptr;
        if (FAILED(disasm(code, len, 0, nullptr, &textBlob)) || !textBlob) return nullptr;
        std::string text(static_cast<const char*>(textBlob->GetBufferPointer()));
        textBlob->Release();

        // The caller only reaches here for a single-layer effect (0/4/5), so the VS is Diffuse/Opaque/
        // EnvMetal -- never the two-layer Composite VS. A `dcl_texcoord1 o` here is a REFLECTION output
        // (EnvMetal), not a second UV set, and must NOT disqualify the patch: EnvMetal is N=1 and its base
        // wants the last UV set just like Diffuse. Only genuine two-UV PASSTHROUGH (two `mov o.xy, v`
        // copies) would be two-layer, and that never reaches a single-layer collection.
        const std::string patched = InjectSingleLayerSet1(text);
        if (patched.empty()) return nullptr;

        ID3DBlob* blob = nullptr;
        ID3DBlob* err  = nullptr;
        const HRESULT hr = assemble(patched.c_str(), patched.size(), "wxlWmoVsSet1", nullptr, nullptr, 0, &blob, &err);
        if (FAILED(hr) || !blob)
        {
            WLOG_WARN("wmo-composite: VS reassemble failed: %s",
                      err ? static_cast<const char*>(err->GetBufferPointer()) : "?");
            if (err) err->Release();
            if (blob) blob->Release();
            return nullptr;
        }
        if (err) err->Release();

        void* wrapper = MakeVsWrapper(blob->GetBufferPointer(), static_cast<uint32_t>(blob->GetBufferSize()));
        blob->Release();
        if (wrapper) WLOG_INFO("wmo-composite: patched single-layer VS to sample UV set 1");
        return wrapper;
    }

    void* GetPatchedVs(void* stock)
    {
        auto it = g_vsPatched.find(stock);
        if (it != g_vsPatched.end()) return it->second;
        void* built = BuildPatchedVs(stock);
        g_vsPatched.emplace(stock, built);
        if (built) g_statVsPatched.fetch_add(1, std::memory_order_relaxed);
        return built;
    }

    /// True when `col` is a single-layer WMO effect collection (Diffuse 0 / Opaque 4 / EnvMetal 5) in
    /// either the AltRender table (modern path) or the exterior table.
    bool IsSingleLayerCollection(void* col)
    {
        for (unsigned i : { 0u, 4u, 5u })
        {
            if (col == *reinterpret_cast<void**>(shoff::kAltEffectTable + i * 4) ||
                col == *reinterpret_cast<void**>(shoff::kExteriorEffectTable + i * 4))
                return true;
        }
        return false;
    }

    // ------------------------------------------------------------------ ground-truth diagnostic
    /// Disassembles one CGxShader wrapper's bytecode and writes it to the log (line by line).
    void LogDisasm(const char* label, void* wrapper)
    {
        if (!wrapper) { WLOG_INFO("wmo-composite DUMP %s: <null>", label); return; }
        const uint8_t* code = At<const uint8_t*>(wrapper, shoff::kCgxShaderBytePtr);
        const uint32_t len  = At<uint32_t>(wrapper, shoff::kCgxShaderByteLen);
        HMODULE comp = Compiler();
        auto disasm = reinterpret_cast<PFN_D3DDisassemble>(comp ? GetProcAddress(comp, "D3DDisassemble") : nullptr);
        ID3DBlob* blob = nullptr;
        if (!code || !disasm || FAILED(disasm(code, len, 0, nullptr, &blob)) || !blob)
        {
            WLOG_INFO("wmo-composite DUMP %s: disasm failed (len=%u)", label, len);
            return;
        }
        std::string text(static_cast<const char*>(blob->GetBufferPointer()));
        blob->Release();
        WLOG_INFO("wmo-composite DUMP %s (%u B):", label, len);
        size_t pos = 0;
        while (pos < text.size())
        {
            size_t eol = text.find('\n', pos);
            if (eol == std::string::npos) eol = text.size();
            std::string line = text.substr(pos, eol - pos);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) WLOG_INFO("  | %s", line.c_str());
            pos = eol + 1;
        }
    }

    bool g_dumped = false;
    /// One-shot: dump the ACTUAL single-layer VS + PS bound in a modern two-UV group, so the right patch
    /// (VS read set1, or PS sample t1) can be chosen from the real shaders rather than guessed.
    void DumpSingleLayerOnce(void* vs, void* ps)
    {
        if (g_dumped) return;
        g_dumped = true;
        LogDisasm("single-layer VS", vs);
        LogDisasm("single-layer PS", ps);
    }

    // ------------------------------------------------------------------ the detours
    /// Frame-stash: record whether the WMO whose batches this leaf is about to draw is modern. The two
    /// entry leaves bracket the delegated (MOHD flag 0x2) path too, so this covers all modern rendering.
    /// Records the modern verdict AND whether the group is in the two-UV vertex format (its VB carries
    /// both UV sets), for the single-layer set1 route. group+0x198 & 8 is the two-UV enable.
    inline bool GroupIsTwoUv(void* group)
    {
        return group && (At<uint32_t>(group, wmo::kOffGroupFormatFlags) & wmo::kGroupFlagTwoUv) != 0;
    }

    void __fastcall hkExtRender(void* root, void* edx, void* group, int flag)
    {
        const bool prevM = g_curModern; const bool prevU = g_curTwoUv;
        g_curModern = g_fixEnabled.load(std::memory_order_relaxed) &&
                      wxl::runtime::wmonative::IsModernRoot(root);
        g_curTwoUv = g_curModern && GroupIsTwoUv(group);
        g_origExtRender(root, edx, group, flag);
        g_curModern = prevM; g_curTwoUv = prevU;
    }

    void __fastcall hkIntRender(void* root, void* edx, void* group, int flag)
    {
        const bool prevM = g_curModern; const bool prevU = g_curTwoUv;
        g_curModern = g_fixEnabled.load(std::memory_order_relaxed) &&
                      wxl::runtime::wmonative::IsModernRoot(root);
        g_curTwoUv = g_curModern && GroupIsTwoUv(group);
        g_origIntRender(root, edx, group, flag);
        g_curModern = prevM; g_curTwoUv = prevU;
    }

    /// Effect-bind post-hook. After the stock VS/PS wrappers are in GxState (0x4D vertex, 0x4E pixel),
    /// override them for a modern WMO batch. TWO routes, one per effect family:
    ///  - Composite (shader 6, two-layer): swap the PIXEL shader (0x4E) for the alpha-composite copy.
    ///  - Diffuse/Opaque/EnvMetal (single-layer) in a two-UV group: swap the VERTEX shader (0x4D) for a
    ///    copy that samples UV set 1, so the single-layer base lands on the group's LAST UV set (Legion's
    ///    rule) without touching the shared vertex buffer.
    /// The immediately following indexed draw flushes the slots; the next batch's bind restores the stock.
    void __cdecl hkEffectBind(uint32_t vtxIdx, uint32_t pixIdx)
    {
        g_origEffectBind(vtxIdx, pixIdx);
        if (!g_curModern) return;

        void* col = *reinterpret_cast<void**>(shoff::kActiveCollection);
        if (!col) return;
        void* gxDev = *reinterpret_cast<void**>(gxoff::kGxDevicePtr);
        if (!gxDev) return;
        auto gxSet = reinterpret_cast<GxSetFn>(shoff::kGxStateSet);

        // Two-layer Composite: pixel-shader swap. Modern WMOs delegate to AltRender, which binds from the
        // 0xD1C3D4 table, so their Composite collection is *kAltEffectComposite; the non-delegated exterior
        // path uses *kExteriorEffectComposite. Accept either.
        if (col == *reinterpret_cast<void**>(shoff::kAltEffectComposite) ||
            col == *reinterpret_cast<void**>(shoff::kExteriorEffectComposite))
        {
            void* stock = *reinterpret_cast<void**>(static_cast<uint8_t*>(col) +
                                                    pixIdx * 4u + shoff::kCollectionPixSlots);
            if (!stock) return;
            void* patched = GetPatched(stock, g_baseUvSet.load(std::memory_order_relaxed));
            if (!patched) return;
            gxSet(gxDev, nullptr, shoff::kStatePixelShader, patched);
            g_statBatches.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        // Single-layer in a two-UV group: vertex-shader swap onto UV set 1.
        if (g_curTwoUv && g_vsRouteEnabled.load(std::memory_order_relaxed) && IsSingleLayerCollection(col))
        {
            void* stockVs = *reinterpret_cast<void**>(static_cast<uint8_t*>(col) +
                                                      vtxIdx * 4u + shoff::kCollectionVtxSlots);
            void* stockPs = *reinterpret_cast<void**>(static_cast<uint8_t*>(col) +
                                                      pixIdx * 4u + shoff::kCollectionPixSlots);
            DumpSingleLayerOnce(stockVs, stockPs); // ground-truth dump of the ACTUAL bound shaders
            if (!stockVs) return;
            void* patchedVs = GetPatchedVs(stockVs);
            if (!patchedVs) return;
            gxSet(gxDev, nullptr, shoff::kStateVertexShader, patchedVs);
            g_statVsSwapped.fetch_add(1, std::memory_order_relaxed);
        }
    }

    bool InstallCompositeShader()
    {
        const bool ext = wxl::hook::Install("WmoComposite.ExtRender", wmo::kExtRender,
                                            &hkExtRender, &g_origExtRender);
        const bool intr = wxl::hook::Install("WmoComposite.IntRender", wmo::kIntRender,
                                             &hkIntRender, &g_origIntRender);
        const bool bind = wxl::hook::Install("WmoComposite.EffectBind", shoff::kEffectBind,
                                             &hkEffectBind, &g_origEffectBind);
        g_installed = ext && intr && bind;
        if (!g_installed)
            WLOG_WARN("wmo-composite: install failed (ext=%d int=%d bind=%d); Composite renders stock",
                      ext ? 1 : 0, intr ? 1 : 0, bind ? 1 : 0);
        else
            WLOG_INFO("wmo-composite: second-layer alpha-composite fix installed");
        return true; // non-fatal
    }
}

// ---------------------------------------------------------------- public query surface
namespace wxl::runtime::wmocomposite
{
    Stats GetStats()
    {
        Stats s{};
        s.patchedShaders = g_statPatched.load(std::memory_order_relaxed);
        s.patchFailures  = g_statPatchFail.load(std::memory_order_relaxed);
        s.batchesSwapped = g_statBatches.load(std::memory_order_relaxed);
        s.vsPatched      = g_statVsPatched.load(std::memory_order_relaxed);
        s.vsSwapped      = g_statVsSwapped.load(std::memory_order_relaxed);
        return s;
    }

    bool Enabled() { return wxl::features::kWmoCompositeAlphaFix; }
    bool Installed() { return g_installed; }

    bool FixEnabled() { return g_fixEnabled.load(std::memory_order_relaxed); }
    void SetFixEnabled(bool on) { g_fixEnabled.store(on, std::memory_order_relaxed); }

    int  BaseUvSet() { return g_baseUvSet.load(std::memory_order_relaxed); }
    void SetBaseUvSet(int set) { g_baseUvSet.store(set & 1, std::memory_order_relaxed); }

    bool VsRouteEnabled() { return g_vsRouteEnabled.load(std::memory_order_relaxed); }
    void SetVsRouteEnabled(bool on) { g_vsRouteEnabled.store(on, std::memory_order_relaxed); }
}

WXL_REGISTER_FEATURE("wmo-composite", wxl::features::kWmoCompositeAlphaFix, InstallCompositeShader)
