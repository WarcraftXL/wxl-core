// Teaches the client to step modern M2 particle emitter records, and to read their zSource sentinel.
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

// MECHANISM
//
// A modern particle emitter record is 0x1EC bytes against the client's 0x1DC, but EVERY field below
// 0x1DC sits at an identical offset -- the 16 extra bytes (multiTexScrollMid/Range) are appended at
// the end. So the client can read a modern record exactly where it lies; the single thing it gets
// wrong is how far to step between records. We therefore adapt the CLIENT, and never touch the data:
// no repack, no side copy, no reshaped array.
//
// Nine instructions in the binary hardcode the 0x1DC step (offsets/game/M2.hpp
// kParticleStrideSites). Each is replaced with a call to a generated thunk that reads the MD20
// header of the model being processed AT THAT SITE and picks the stride from its version. That
// matters: CM2Scene::Animate walks a chain of models, so a stride chosen once per call -- in a
// thread-local, say -- would be wrong for every model after the first in a scene that mixes stock
// v264 doodads with modern ones, which is the permanent state on a modern map.
//
// Thunk shape (integer-only: x87 state is live at three of the sites):
//     push eax
//     mov  eax, <header>          ; register, or [ebp+disp]
//     mov  eax, [eax+4]           ; MD20 inner version
//     cmp  eax, 271
//     mov  eax, 0x1DC
//     jbe  +5                     ; <= 271 keeps the client stride
//     mov  eax, 0x1EC
//     <original op, register form> ; imul esi,eax | add ecx,eax | add ebx,eax | add [ebp+d],eax
//     pop  eax
//     ret
//
// The gate is the VERSION, not globalFlags & 0x200: not one modern model in the corpus sets that
// bit, so testing it would silently hand back the client stride for every one of them.
//
// Flags are dead at all nine sites (each is followed by another flag-setting instruction before any
// conditional branch), so the thunk does not reproduce OF/CF/ZF.

#include "config.hpp"

#include "common/Log.hpp"
#include "common/Mem.hpp"
#include "engine/hook/Hook.hpp"
#include "engine/hook/Registry.hpp"
#include "features/m2native/ParticleStride.hpp"
#include "offsets/game/M2.hpp"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>

namespace
{
    namespace off = wxl::offsets::game::m2;

    constexpr uint32_t kThunkSlot  = 64; // generous; the longest thunk is under 32 bytes
    constexpr uint8_t  kNop        = 0x90;

    /// Stride thunks occupy the first slots; the two textureId-unpack thunks follow them.
    constexpr uint32_t kThunkSlotsForStride =
        static_cast<uint32_t>(sizeof(off::kParticleStrideSites) / sizeof(off::kParticleStrideSites[0]));
    constexpr uint32_t kThunkSlotsTotal = kThunkSlotsForStride + 2;

    uint8_t* g_thunks = nullptr;
    uint32_t g_patched = 0;
    bool     g_installed = false;
    bool     g_texIdPatched = false;

    std::atomic<bool>     g_zSourceFix{ true };
    std::atomic<uint32_t> g_statZSourceNeutralized{ 0 };
    off::M2_SetZsourceFn  g_origSetZsource = nullptr;

    /// `mov eax, <reg>` encodings, indexed by ParticleStrideSite::headerReg (1..5).
    /// 0 is the [ebp+disp] form and is emitted separately.
    constexpr uint8_t kMovEaxFromReg[6] = { 0x00, 0xC3, 0xC6, 0xC2, 0xC1, 0xC7 };
    //                                       --    ebx   esi   edx   ecx   edi

    struct Emitter
    {
        uint8_t* p;
        void Byte(uint8_t b) { *p++ = b; }
        void Dword(uint32_t v) { std::memcpy(p, &v, 4); p += 4; }
    };

    /**
     * @brief Writes one thunk and returns its address.
     * @param dst   destination inside the executable page.
     * @param site  the site this thunk stands in for.
     */
    uint8_t* BuildThunk(uint8_t* dst, const off::ParticleStrideSite& site)
    {
        Emitter e{ dst };
        e.Byte(0x50);                                   // push eax

        if (site.headerReg == 0)                        // mov eax, [ebp+disp]
        {
            e.Byte(0x8B); e.Byte(0x45); e.Byte(static_cast<uint8_t>(site.disp));
        }
        else                                            // mov eax, <reg>
        {
            e.Byte(0x8B); e.Byte(kMovEaxFromReg[site.headerReg]);
        }

        e.Byte(0x8B); e.Byte(0x40); e.Byte(0x04);       // mov eax, [eax+4]   (inner version)
        e.Byte(0x3D); e.Dword(off::kParticleModernMinVer - 1);   // cmp eax, 271
        e.Byte(0xB8); e.Dword(off::kParticleStrideClient);       // mov eax, 0x1DC
        e.Byte(0x76); e.Byte(0x05);                     // jbe +5  (version <= 271)
        e.Byte(0xB8); e.Dword(off::kParticleStrideModern);       // mov eax, 0x1EC

        switch (site.opKind)
        {
        case 0: e.Byte(0x0F); e.Byte(0xAF); e.Byte(0xF0); break;                 // imul esi, eax
        case 1: e.Byte(0x01); e.Byte(0xC1); break;                               // add  ecx, eax
        case 2: e.Byte(0x01); e.Byte(0xC3); break;                               // add  ebx, eax
        default: e.Byte(0x01); e.Byte(0x45);
                 e.Byte(static_cast<uint8_t>(site.disp)); break;                 // add  [ebp+d], eax
        }

        e.Byte(0x58);                                   // pop eax
        e.Byte(0xC3);                                   // ret
        return dst;
    }

    /**
     * @brief Verifies a site still holds the stock instruction, then redirects it at its thunk.
     *
     * Refusing to patch on a byte mismatch is the point: a different client build, or a second
     * install pass, must fail loudly rather than corrupt a code path that runs on a worker thread.
     */
    bool PatchSite(const off::ParticleStrideSite& site, const uint8_t* thunk)
    {
        auto* at = reinterpret_cast<uint8_t*>(site.va);

        // The stride immediate sits at the tail of the instruction in every form we patch.
        uint32_t imm = 0;
        std::memcpy(&imm, at + site.length - 4, 4);
        if (imm != off::kParticleStrideClient)
        {
            WLOG_ERROR("m2particles: site 0x%08X does not hold the stock 0x%X stride (found 0x%X) -- not patching",
                       static_cast<unsigned>(site.va), off::kParticleStrideClient, imm);
            return false;
        }

        uint8_t patch[7];
        std::memset(patch, kNop, sizeof patch);
        patch[0] = 0xE8;                                // call rel32
        const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(thunk) -
                                                 (site.va + 5));
        std::memcpy(patch + 1, &rel, 4);
        wxl::mem::Patch(at, patch, site.length);
        return true;
    }

    /**
     * @brief Replaces one whole instruction with a call to a thunk, verifying the stock bytes first.
     * @param va        instruction address.
     * @param len       its length (>= 5, so a call rel32 fits; remainder padded with nop).
     * @param expect    the stock bytes, so a different build fails loudly instead of corrupting code.
     * @param thunk     the replacement.
     */
    bool PatchInstruction(uintptr_t va, uint32_t len, const uint8_t* expect, const uint8_t* thunk)
    {
        auto* at = reinterpret_cast<uint8_t*>(va);
        if (std::memcmp(at, expect, len) != 0)
        {
            WLOG_ERROR("m2particles: 0x%08X does not hold the expected stock instruction -- not patching",
                       static_cast<unsigned>(va));
            return false;
        }
        uint8_t patch[8];
        std::memset(patch, kNop, sizeof patch);
        patch[0] = 0xE8;
        const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(thunk) - (va + 5));
        std::memcpy(patch + 1, &rel, 4);
        wxl::mem::Patch(at, patch, len);
        return true;
    }

    /**
     * @brief Teaches the two textureId read sites to unpack a packed multi-texture id.
     *
     * Each thunk redoes the replaced instruction's work, then masks the id to its low 5 bits when the
     * emitter's flag 0x10000000 says the field is three packed ids. The record is only READ.
     * @return true when both sites were patched.
     */
    bool PatchTextureIdSites()
    {
        // --- InitializeLoaded: replaces `mov ecx,[eax+0x174]` (edx already holds the raw id,
        //     esi is the emitter record base, eax the CM2Shared).
        static const uint8_t kExpectInit[6] = { 0x8B, 0x88, 0x74, 0x01, 0x00, 0x00 };
        uint8_t* t1 = g_thunks + kThunkSlotsForStride * kThunkSlot;
        {
            Emitter e{ t1 };
            e.Byte(0x8B); e.Byte(0x88); e.Dword(0x174);              // mov  ecx, [eax+0x174]
            e.Byte(0xF7); e.Byte(0x46); e.Byte(0x04);
            e.Dword(off::kParticleFlagMultiTex);                     // test dword [esi+4], 0x10000000
            e.Byte(0x74); e.Byte(0x03);                              // jz   +3
            e.Byte(0x83); e.Byte(0xE2); e.Byte(0x1F);                // and  edx, 0x1F
            e.Byte(0xC3);                                            // ret
        }

        // --- ReplaceTexture: replaces `movzx eax, word [ecx+edx+0x16]`; ecx+edx is the record base.
        static const uint8_t kExpectRepl[5] = { 0x0F, 0xB7, 0x44, 0x11, 0x16 };
        uint8_t* t2 = t1 + kThunkSlot;
        {
            Emitter e{ t2 };
            e.Byte(0x0F); e.Byte(0xB7); e.Byte(0x44); e.Byte(0x11); e.Byte(0x16); // movzx eax,[ecx+edx+0x16]
            e.Byte(0xF7); e.Byte(0x44); e.Byte(0x11); e.Byte(0x04);
            e.Dword(off::kParticleFlagMultiTex);                     // test dword [ecx+edx+4], flag
            e.Byte(0x74); e.Byte(0x03);                              // jz   +3
            e.Byte(0x83); e.Byte(0xE0); e.Byte(0x1F);                // and  eax, 0x1F
            e.Byte(0xC3);                                            // ret
        }

        const bool a = PatchInstruction(off::kParticleTexIdInitLoaded, off::kParticleTexIdInitLen,
                                        kExpectInit, t1);
        const bool b = PatchInstruction(off::kParticleTexIdReplaceTex, off::kParticleTexIdReplaceLen,
                                        kExpectRepl, t2);
        return a && b;
    }

    /**
     * @brief Neutralizes the modern zSource sentinel at the one place the client consumes it.
     *
     * Modern content writes zSource = 255.0 to mean "no point source". The client tests `== 0.0`, so
     * it takes the point-source branch instead: it discards the emission cone and overwrites the
     * spawn position with a normalized (pos - (0,0,255)) direction -- which is why modern fire drifts
     * sideways rather than rising. Rewriting the value here fixes both the load path and the
     * per-frame path (this function has exactly two callers) and leaves the model bytes untouched.
     */
    void __fastcall hkSetZsource(void* emitter, void* edx, float zSource)
    {
        if (g_zSourceFix.load(std::memory_order_relaxed) && zSource == 255.0f)
        {
            zSource = 0.0f;
            g_statZSourceNeutralized.fetch_add(1, std::memory_order_relaxed);
        }
        g_origSetZsource(emitter, edx, zSource);
    }

    bool InstallParticleStride()
    {
        constexpr uint32_t kSiteCount = kThunkSlotsForStride;

        g_thunks = static_cast<uint8_t*>(VirtualAlloc(nullptr, kThunkSlotsTotal * kThunkSlot,
                                                      MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!g_thunks)
        {
            WLOG_ERROR("m2particles: thunk page allocation failed -- emitters stay parked");
            return false;
        }
        std::memset(g_thunks, 0xCC, kThunkSlotsTotal * kThunkSlot);

        for (uint32_t i = 0; i < kSiteCount; ++i)
        {
            const auto& site = off::kParticleStrideSites[i];
            uint8_t* thunk = BuildThunk(g_thunks + i * kThunkSlot, site);
            if (PatchSite(site, thunk)) ++g_patched;
        }

        // All or nothing: a partially patched set would step some sites at 0x1DC and others at
        // 0x1EC over the same array, which is worse than leaving emitters parked.
        if (g_patched != kSiteCount)
        {
            WLOG_ERROR("m2particles: only %u/%u stride sites patched -- modern emitters stay parked",
                       g_patched, kSiteCount);
            return false;
        }

        // The other precondition for un-parking: the client must unpack a packed multi-texture
        // textureId at its read sites, or it indexes the handle table out of bounds and faults.
        g_texIdPatched = PatchTextureIdSites();
        if (!g_texIdPatched)
        {
            WLOG_ERROR("m2particles: textureId read sites not patched -- modern emitters stay parked");
            return false;
        }

        wxl::hook::Install("M2SetZsource", off::kSetZsource, &hkSetZsource, &g_origSetZsource);

        g_installed = true;
        WLOG_INFO("m2particles: %u stride sites + 2 textureId read sites redirected "
                  "(client %#x / modern %#x, gate version > %u); zSource sentinel neutralized at 0x%08X",
                  g_patched, off::kParticleStrideClient, off::kParticleStrideModern,
                  off::kParticleModernMinVer - 1, static_cast<unsigned>(off::kSetZsource));
        return true;
    }
}

namespace wxl::runtime::m2particles
{
    bool Installed() { return g_installed; }

    bool TextureIdSitesPatched() { return g_texIdPatched; }


    bool ZSourceFixEnabled() { return g_zSourceFix.load(std::memory_order_relaxed); }

    void SetZSourceFixEnabled(bool on)
    {
        if (g_zSourceFix.exchange(on, std::memory_order_relaxed) != on)
            WLOG_INFO("m2particles: zSource sentinel fix %s", on ? "ENABLED" : "disabled (stock)");
    }

    Stats GetStats()
    {
        Stats s{};
        s.sitesPatched       = g_patched;
        s.zSourceNeutralized = g_statZSourceNeutralized.load(std::memory_order_relaxed);
        return s;
    }
}

WXL_REGISTER_FEATURE("m2native-particles", wxl::features::kNativeM2Particles, InstallParticleStride)
