// Grass motion: disassemble the real DetailDoodad vertex shader, inject a per-blade wind + repulsion
// block, reassemble it, and swap it in at IShaderCreateVertex. Constants ride the engine's own upload.
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

#include "features/grasswind/GrassWind.hpp"

#include "config.hpp"
#include "engine/hook/Hook.hpp"
#include "engine/hook/Registry.hpp"
#include "engine/events/Event.hpp"

#include "common/Log.hpp"
#include "offsets/game/GroundEffect.hpp"
#include "offsets/engine/Shader.hpp"
#include "game/camera/Camera.hpp"
#include "game/world/World.hpp"

#include <windows.h>
#include <d3dcompiler.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    namespace ge    = wxl::offsets::game::groundeffect;
    namespace sh    = wxl::offsets::engine::shader;
    namespace ev    = wxl::events;
    namespace cam   = wxl::game::camera;
    namespace world = wxl::game::world;

    // D3DAssemble / D3DDisassemble live in d3dcompiler_47.dll; D3DAssemble has no SDK header declaration,
    // so both are resolved by name (the compiler is already present — the modern asset path uses it).
    typedef HRESULT(WINAPI* PFN_D3DAssemble)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*,
                                             UINT, ID3DBlob**, ID3DBlob**);
    typedef HRESULT(WINAPI* PFN_D3DDisassemble)(LPCVOID, SIZE_T, UINT, LPCSTR, ID3DBlob**);

    constexpr float kEps    = 1.0e-3f; // guards 1/dist at the repulsion centre
    constexpr float kJitter = 1.7f;    // per-blade phase spread, radians
    constexpr float kTwoPi  = 6.2831853f;

    // Exact byte lengths of the six stock DetailDoodad blobs per profile
    // length + version token uniquely identifies a grass
    // vertex shader at create time. No length is shared between the profiles.
    constexpr uint32_t kStockLen_vs20[6] = { 552, 644, 788, 1428, 1520, 1664 };
    constexpr uint32_t kStockLen_vs30[6] = { 548, 676, 844, 1328, 1456, 1624 };

    wxl::features::grasswind::WindSettings    g_wind;
    wxl::features::grasswind::PhysicsSettings g_physics;
    bool                                      g_installed  = false;
    bool                                      g_playerValid = false;

    ge::InitShaderConstantsFn g_origInitConstants = nullptr;
    sh::ShaderCreateVertexFn  g_origCreateVertex  = nullptr;

    // Cache of reassembled replacements, keyed by the stock blob length (unique per variant).
    struct CacheEntry { uint32_t stockLen; ID3DBlob* blob; };
    std::vector<CacheEntry> g_cache;

    // Injected after the world position lands in view-space r0, before the projection. The grass
    // modelview 3x3 (c0/c1/c2) is the pure view rotation R (batches are unrotated), so worldPos =
    // R^T * r0 + cameraWorld. The world-plane offset is the sum of the player repulsion and two
    // travelling sine waves, rotated back into view space; both anchor the blade base:
    //  - repulsion weight = a height cone above the player's ground Z;
    //  - wind weight = 1 - uv.y (blade v runs 1 at the base to 0 at the tip), anchored and squared,
    //    so it is exact per blade on any terrain, times a view-distance fade and a per-blade variance.
    // r0/r1 are the only temps the grass shader uses, so r2..r5 are free scratch. c13.y = 1.0 is the
    // engine shader's own def and survives in the copy. Constants live in the free block c14..c21.
    const char* kMotionInject =
        "    dp3 r2.x, c0, r0\n"
        "    dp3 r2.y, c1, r0\n"
        "    add r2.xy, r2, c14\n"
        "    mov r4.xy, r2\n"
        "    dp3 r3.z, c2, r0\n"
        "    add r3.z, r3.z, c14.z\n"
        "    add r3.z, r3.z, -c16.z\n"
        "    add r3.z, r3.z, -c15.z\n"
        "    mul_sat r4.z, r3.z, c15.x\n"
        "    add r2.xy, r2, -c16\n"
        "    mul r2.z, r2.x, r2.x\n"
        "    mad r2.z, r2.y, r2.y, r2.z\n"
        "    add r2.z, r2.z, c15.w\n"
        "    rsq r2.w, r2.z\n"
        "    mul r3.x, r2.z, r2.w\n"
        "    mul_sat r3.y, r3.x, c15.y\n"
        "    mad r3.y, c17.x, r3.y, c17.y\n"
        "    slt r3.z, r3.x, c17.z\n"
        "    mul r3.y, r3.y, r3.z\n"
        "    mul r3.y, r3.y, r4.z\n"
        "    mul r3.y, r3.y, r2.w\n"
        "    mul r2.xy, r2, r3.y\n"
        "    add r5.z, c13.y, -v3.y\n"
        "    add r5.z, r5.z, c21.z\n"
        "    mul_sat r5.z, r5.z, c21.w\n"
        "    mul r5.z, r5.z, r5.z\n"
        "    mov r5.w, c13.y\n"
        "    mad r5.w, r0.z, c20.w, r5.w\n"
        "    rcp r5.w, r5.w\n"
        "    mul r5.z, r5.z, r5.w\n"
        "    mul r3.x, r4.x, c20.x\n"
        "    mad r3.x, r4.y, c20.y, r3.x\n"
        "    frc r3.x, r3.x\n"
        "    mad r2.z, r3.x, c21.y, c21.x\n"
        "    mul r5.z, r5.z, r2.z\n"
        "    mul r3.x, r3.x, c20.z\n"
        "    mul r3.y, r4.x, c18.z\n"
        "    mad r3.y, r4.y, c18.w, r3.y\n"
        "    add r3.y, r3.y, r3.x\n"
        "    add r3.y, r3.y, c17.w\n"
        "    mul r3.z, r4.x, c19.z\n"
        "    mad r3.z, r4.y, c19.w, r3.z\n"
        "    add r3.z, r3.z, r3.x\n"
        "    add r3.z, r3.z, c14.w\n"
        "    sincos r4.xy, r3.y\n"
        "    add r4.w, r4.y, c16.w\n"
        "    sincos r5.xy, r3.z\n"
        "    mul r3.zw, c18.xxxy, r4.wwww\n"
        "    mad r3.zw, c19.xxxy, r5.yyyy, r3\n"
        "    mul r3.zw, r3, r5.zzzz\n"
        "    add r2.xy, r2, r3.zwzw\n"
        "    mul r3.xyz, c0, r2.x\n"
        "    mad r3.xyz, c1, r2.y, r3.xyz\n"
        "    add r0.xyz, r0, r3\n";

    HMODULE Compiler()
    {
        HMODULE d = GetModuleHandleA("d3dcompiler_47.dll");
        return d ? d : LoadLibraryA("d3dcompiler_47.dll");
    }

    // Identifies a stock DetailDoodad vertex shader by version token + exact blob length.
    bool IsStockGrassVS(const unsigned char* code, uint32_t len)
    {
        if (!code || len < 8)
            return false;
        uint32_t token;
        std::memcpy(&token, code, sizeof(token));
        if (token == 0xFFFE0200u)
            for (uint32_t l : kStockLen_vs20) if (l == len) return true;
        if (token == 0xFFFE0300u)
            for (uint32_t l : kStockLen_vs30) if (l == len) return true;
        return false;
    }

    // Disassembles a stock blob, injects the motion block after the view-transform anchor, reassembles,
    // and caches the result by stock length. Returns the reassembled blob (owned by the cache) or null.
    ID3DBlob* BuildInjected(const unsigned char* stock, uint32_t len)
    {
        for (const CacheEntry& e : g_cache)
            if (e.stockLen == len)
                return e.blob;

        HMODULE comp = Compiler();
        auto disasm  = reinterpret_cast<PFN_D3DDisassemble>(comp ? GetProcAddress(comp, "D3DDisassemble") : nullptr);
        auto assemble = reinterpret_cast<PFN_D3DAssemble>(comp ? GetProcAddress(comp, "D3DAssemble") : nullptr);
        if (!disasm || !assemble)
        {
            WLOG_WARN("grasswind: d3dcompiler_47 D3DDisassemble/D3DAssemble unavailable");
            return nullptr;
        }

        ID3DBlob* text = nullptr;
        if (FAILED(disasm(stock, len, 0, nullptr, &text)) || !text)
            return nullptr;

        std::string src(static_cast<const char*>(text->GetBufferPointer()));
        text->Release();

        size_t at = src.find("add r0, r0, c3");
        if (at == std::string::npos) { WLOG_WARN("grasswind: injection anchor missing (len=%u)", len); return nullptr; }
        at = src.find('\n', at);
        if (at == std::string::npos) return nullptr;
        src.insert(at + 1, kMotionInject);

        ID3DBlob* code  = nullptr;
        ID3DBlob* error = nullptr;
        HRESULT   hr    = assemble(src.c_str(), src.size(), "grassMotion", nullptr, nullptr, 0, &code, &error);
        if (FAILED(hr) || !code)
        {
            WLOG_WARN("grasswind: assemble failed (len=%u): %s", len,
                      error ? static_cast<const char*>(error->GetBufferPointer()) : "?");
            if (error) error->Release();
            if (code)  code->Release();
            return nullptr;
        }
        if (error) error->Release();

        g_cache.push_back({ len, code }); // kept alive for the life of the process
        WLOG_INFO("grasswind: injected motion into DetailDoodad VS (stock len=%u -> %zu)", len, code->GetBufferSize());
        return code;
    }

    // Reads the player world position; false when no player is resident.
    bool ReadPlayer(float out[3])
    {
        unsigned long long guid = world::ActivePlayerGuid();
        if (!guid) return false;
        void* unit = world::ResolveObject(guid, world::kTypeMaskPlayer);
        if (!unit) return false;
        world::UnitPosition(unit, out);
        return true;
    }

    // Writes the motion constants into the engine's free block region (c14..c21). The engine memsets the
    // block and fills c0..c13 in InitializeShaderConstants (whose tail this runs from), leaving c14+ zero,
    // then ships the whole block with its own per-chunk upload — so one write per frame reaches every
    // grass draw with no extra device call. Wind always writes; repulsion zeroes out when no player.
    void WriteFrameConstants()
    {
        float camPos[3];
        cam::Position(camPos);

        float player[3] = { camPos[0], camPos[1], camPos[2] };
        g_playerValid = ReadPlayer(player);

        const wxl::features::grasswind::WindSettings&    w = g_wind;
        const wxl::features::grasswind::PhysicsSettings& p = g_physics;

        const float t   = (GetTickCount() % 1000000u) * 0.001f;
        const float a1  = w.directionDeg * (kTwoPi / 360.0f);
        const float a2  = (w.directionDeg + w.crossAngleDeg) * (kTwoPi / 360.0f);
        const float k1  = kTwoPi / (w.wavelength      > 0.1f ? w.wavelength      : 0.1f);
        const float k2  = kTwoPi / (w.crossWavelength > 0.1f ? w.crossWavelength : 0.1f);
        const float d1[2] = { std::cos(a1), std::sin(a1) };
        const float d2[2] = { std::cos(a2), std::sin(a2) };
        const float amp1  = w.enabled ? w.amplitude      : 0.0f;
        const float amp2  = w.enabled ? w.crossAmplitude : 0.0f;
        const float lean  = w.enabled ? w.lean           : 0.0f;
        const float phase1 = -std::fmod(k1 * w.speed * t, kTwoPi);
        const float phase2 = -std::fmod(k2 * w.speed * 1.37f * t, kTwoPi);

        const bool  rep    = p.enabled && g_playerValid;
        const float center = rep ? p.forceCenter : 0.0f;
        const float edge   = rep ? p.forceEdge   : 0.0f;
        const float coneH  = p.coneHeight > 0.01f ? p.coneHeight : 0.01f;
        const float radius = p.radius     > 0.01f ? p.radius     : 0.01f;

        float var = w.variance;
        if (var < 0.0f) var = 0.0f;
        if (var > 1.0f) var = 1.0f;

        float anchor = w.anchor;
        if (anchor < 0.0f) anchor = 0.0f;
        if (anchor > 0.9f) anchor = 0.9f;

        const float c[8][4] = {
            { camPos[0], camPos[1], camPos[2], phase2 },                    // c14
            { 1.0f / coneH, 1.0f / radius, p.minHeight, kEps },             // c15
            { player[0], player[1], player[2], lean },                      // c16
            { edge - center, center, radius, phase1 },                      // c17
            { d1[0] * amp1, d1[1] * amp1, d1[0] * k1, d1[1] * k1 },         // c18
            { d2[0] * amp2, d2[1] * amp2, d2[0] * k2, d2[1] * k2 },         // c19
            { 0.737f, 1.311f, kJitter, w.distanceFade > 0.0f ? w.distanceFade : 0.0f }, // c20
            { 1.0f - var * 0.5f, var, -anchor, 1.0f / (1.0f - anchor) },    // c21
        };
        float* block = reinterpret_cast<float*>(ge::kVsConstantBlock) + ge::kVsFirstFreeReg * 4;
        std::memcpy(block, c, sizeof(c));

        if (ev::Any(ev::Event::OnGrassWind))
        {
            ev::GrassWindArgs a{ d1[0] * amp1, d1[1] * amp1, w.amplitude, phase1 };
            ev::Emit(ev::Event::OnGrassWind, &a);
        }
    }

    // Tail hook on the once-per-frame grass constant setup: run the original (fills c0..c13, zeroes
    // c14+), then write the motion constants into c14..c21 for the engine's own upload to ship.
    void __cdecl hkInitShaderConstants()
    {
        g_origInitConstants();
        WriteFrameConstants();
    }

    // Detour on the per-shader D3D vertex-shader create: if the wrapper carries a stock DetailDoodad blob,
    // build (once, cached) an injected replacement from that exact blob and swap the wrapper's bytecode
    // pointer/length to it for the create, then restore the fields. Unrecognised shaders pass through.
    void __fastcall hkShaderCreateVertex(void* device, void* edx, void* wrapper)
    {
        if (wrapper)
        {
            auto* byteLen = reinterpret_cast<uint32_t*>(reinterpret_cast<char*>(wrapper) + sh::kCgxShaderByteLen);
            auto* bytePtr = reinterpret_cast<const unsigned char**>(reinterpret_cast<char*>(wrapper) + sh::kCgxShaderBytePtr);

            if (*bytePtr && IsStockGrassVS(*bytePtr, *byteLen))
            {
                ID3DBlob* repl = BuildInjected(*bytePtr, *byteLen);
                if (repl)
                {
                    const unsigned char* savedPtr = *bytePtr;
                    const uint32_t       savedLen = *byteLen;
                    *bytePtr = static_cast<const unsigned char*>(repl->GetBufferPointer());
                    *byteLen = static_cast<uint32_t>(repl->GetBufferSize());
                    g_origCreateVertex(device, edx, wrapper);
                    *bytePtr = savedPtr;
                    *byteLen = savedLen;
                    g_installed = true;
                    return;
                }
            }
        }
        g_origCreateVertex(device, edx, wrapper);
    }

    bool InstallGrassWind()
    {
        const bool a = wxl::hook::Install("GrassWind.ShaderCreateVertex", sh::kShaderCreateVertex,
                                          &hkShaderCreateVertex, &g_origCreateVertex);
        const bool b = wxl::hook::Install("GrassWind.InitShaderConstants", ge::kInitShaderConstants,
                                          &hkInitShaderConstants, &g_origInitConstants);
        if (a && b)
            WLOG_INFO("grasswind: shader-inject + constant hooks installed (Milestone B)");
        else
            WLOG_WARN("grasswind: hook install failed (create=%d constants=%d); grass renders stock", a, b);
        return true; // non-fatal
    }
}

namespace wxl::features::grasswind
{
    WindSettings&    Wind()      { return g_wind; }
    PhysicsSettings& Physics()   { return g_physics; }
    bool             Installed() { return g_installed; }
}

WXL_REGISTER_FEATURE("grasswind", wxl::features::kGrassWind, InstallGrassWind)
