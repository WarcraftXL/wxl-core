// Doodad and WMO spawn/parse detours: publish placement events and apply the per-instance MODF scale.
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

#include "config.hpp"
#include "engine/hook/Hook.hpp"
#include "engine/hook/Registry.hpp"
#include "engine/events/Event.hpp"
#include "features/diag/AssetProfile.hpp"

#include "common/Log.hpp"
#include "offsets/game/Doodad.hpp"
#include "offsets/game/WMO.hpp"

#include <cstdint>

namespace
{
    namespace ev    = wxl::events;
    namespace dd    = wxl::offsets::game::doodad;
    namespace wmo   = wxl::offsets::game::wmo;
    namespace aprof = wxl::runtime::assetprof;

    dd::SpawnFromMDDFFn      g_origDoodadSpawn = nullptr;
    wmo::Wmo_SpawnFromModfFn g_origWmoSpawn    = nullptr;
    wmo::Wmo_RootCompleteFn  g_origWmoRoot     = nullptr;
    wmo::WmoGroup_ParseFn    g_origWmoGroup    = nullptr;

    /**
     * @brief Detours doodad spawn, emitting OnDoodadSpawn with the placement the native call built.
     * @param modelName   doodad model name.
     * @param mddf        placement record.
     * @param tileOrigin  tile origin for the placement.
     * @return the spawned doodad.
     */
    void* __cdecl hkDoodadSpawn(const char* modelName, void* mddf, void* tileOrigin)
    {
        void* doodad = g_origDoodadSpawn(modelName, mddf, tileOrigin);
        ev::DoodadSpawnArgs a{ doodad };
        ev::Emit(ev::Event::OnDoodadSpawn, &a);
        return doodad;
    }

    /** @brief Multiplies the upper-left 3x3 rows of a 4x4 row-major matrix by a uniform factor. */
    void ScaleMatrixRows3x3(float* m, float s)
    {
        m[0] *= s; m[1] *= s; m[2]  *= s;
        m[4] *= s; m[5] *= s; m[6]  *= s;
        m[8] *= s; m[9] *= s; m[10] *= s;
    }

    /**
     * @brief Reports a freshly spawned WMO's live doodad-set selection against its loaded MODS.
     *
     * Catches the in-game "all doodad sets render at once" case by reading the post-down-convert MODS the
     * Client actually loaded (not the on-disk file) plus the instance's selected/extra sets. Logs only a
     * suspicious shape: an extra set populated, a selected index out of range, or a set 0 whose MODD range
     * swallows the other content sets (every doodad then resolves to set 0, which renders unconditionally).
     * A correctly selected WMO stays silent.
     * @param inst  freshly spawned WMO instance.
     */
    void DiagDoodadSets(void* inst)
    {
        auto* root = *reinterpret_cast<void**>(static_cast<uint8_t*>(inst) + wmo::kOffInstanceRoot);
        if (!root)
            return;
        const uint32_t nSets = *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(root) + wmo::kOffRootDoodadSets);
        if (nSets < 2)
            return; // a single-set WMO cannot show "extra" sets
        const uint8_t* mods = *reinterpret_cast<uint8_t**>(static_cast<uint8_t*>(root) + wmo::kOffRootMods);
        if (!mods)
            return;
        const uint32_t nDefs = *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(root) + wmo::kOffRootDoodadDefs);
        const uint32_t sel   = *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(inst) + wmo::kOffInstanceDoodadSet);
        const uint16_t* extra = reinterpret_cast<uint16_t*>(static_cast<uint8_t*>(inst) + wmo::kOffInstanceExtraSets);
        const char* name = reinterpret_cast<const char*>(static_cast<uint8_t*>(root) + wmo::kOffNameInline);
        const uint32_t s0count = *reinterpret_cast<const uint32_t*>(mods + wmo::kOffModsCount);

        const bool greedy0  = nDefs && s0count + 1 >= nDefs;    // set 0 covers (almost) every def
        const bool selOob   = sel >= nSets;                     // selected index out of range
        const bool hasExtra = extra[0] || extra[1] || extra[2]; // extra sets populated
        if (!greedy0 && !selOob && !hasExtra)
            return; // selection resolves to {set0, sel} only -> correct, stay silent

        WLOG_INFO("wmo-doodad-diag: %.96s nSets=%u nDefs=%u sel=%u extra={%u,%u,%u} set0count=%u%s%s%s",
            name, nSets, nDefs, sel, extra[0], extra[1], extra[2], s0count,
            greedy0 ? " GREEDY-SET0" : "", selOob ? " SEL-OOB" : "", hasExtra ? " EXTRA-SETS" : "");
    }

    /**
     * @brief Detours the WMO instance spawn, applying the per-instance MODF scale the Client ignores.
     *
     * The Client builds the instance at scale 1.0 (MODF+0x3E is padding to it). After the native spawn,
     * the modern scale is folded into the render matrix (+0x70). The collision/portal copy (+0xB0) is a
     * transposed read-back the portal-visibility test reads as an inverse rotation; scaling it breaks that
     * test and culls interior groups (the WMO goes invisible), so it is left at 1.0 (collision stays at
     * native size). A dedup hit returns an already-scaled instance, so the scale is applied only to a
     * freshly built instance, recognised by its still-orthonormal basis (|row0| == 1); this is reload-safe
     * and needs no per-instance bookkeeping.
     * @param ctx         world context.
     * @param modf        MODF placement record.
     * @param tileOrigin  tile world origin.
     * @param dedup       non-zero to return an existing instance for a known uniqueId.
     * @return the spawned (or existing) instance.
     */
    void* __cdecl hkWmoSpawn(void* ctx, void* modf, const float* tileOrigin, int dedup)
    {
        void* inst = g_origWmoSpawn(ctx, modf, tileOrigin, dedup);
        if (!inst || !modf)
            return inst;

        DiagDoodadSets(inst);

        const uint16_t raw = *reinterpret_cast<uint16_t*>(static_cast<uint8_t*>(modf) + wmo::kOffModfScale);
        if (raw == 0 || raw == 1024)
            return inst; // native / unscaled: leave the instance byte-for-byte
        const float s = static_cast<float>(raw) / 1024.0f;

        float* render = reinterpret_cast<float*>(static_cast<uint8_t*>(inst) + wmo::kOffInstanceRenderMatrix);
        const float len2 = render[0] * render[0] + render[1] * render[1] + render[2] * render[2];
        if (len2 < 0.9999f || len2 > 1.0001f)
            return inst; // already scaled (a dedup hit returned an existing instance)

        ScaleMatrixRows3x3(render, s);
        return inst;
    }

    /**
     * @brief Detours WMO root read-completion, emitting OnWmoRootLoad before the native chunk walk.
     *
     * Fires once after the async read fills the root buffer and before the walker runs, so a subscriber
     * may reshape the root buffer in place; the native walk then reads the reshaped bytes.
     * @param root  map-object root whose buffer was just read.
     */
    void __cdecl hkWmoRootComplete(void* root)
    {
        const uint64_t preStarted = aprof::Now();
        ev::WmoRootLoadArgs a{ root };
        ev::Emit(ev::Event::OnWmoRootLoad, &a);
        if (preStarted) aprof::Record(aprof::Phase::WmoRootPre, aprof::Now() - preStarted);
        const uint64_t nativeStarted = aprof::Now();
        g_origWmoRoot(root);
        if (nativeStarted) aprof::Record(aprof::Phase::WmoRootNative, aprof::Now() - nativeStarted);
    }

    /**
     * @brief Detours WMO group parse, emitting OnWmoGroupLoad before the native sub-chunk walk.
     *
     * The join point of the sync and async group-load paths, before the sub-chunk walk, so a subscriber
     * may reshape the group buffer in place; the native walk then reads the reshaped bytes.
     * @param group  map-object group whose buffer was just read.
     * @param edx    unused register slot for the thiscall convention.
     */
    void __fastcall hkWmoGroupParse(void* group, void* edx)
    {
        const uint64_t preStarted = aprof::Now();
        ev::WmoGroupLoadArgs a{ group };
        ev::Emit(ev::Event::OnWmoGroupLoad, &a);
        if (preStarted) aprof::Record(aprof::Phase::WmoGroupPre, aprof::Now() - preStarted);
        const uint64_t nativeStarted = aprof::Now();
        g_origWmoGroup(group, edx);
        if (nativeStarted) aprof::Record(aprof::Phase::WmoGroupNative, aprof::Now() - nativeStarted);
    }

    bool InstallSpawn()
    {
        wxl::hook::Install("DoodadSpawn", dd::kSpawnFromMDDF, &hkDoodadSpawn, &g_origDoodadSpawn);
        wxl::hook::Install("WmoSpawn", wmo::kSpawnFromModf, &hkWmoSpawn, &g_origWmoSpawn);
        wxl::hook::Install("WmoRootComplete", wmo::kRootComplete, &hkWmoRootComplete, &g_origWmoRoot);
        wxl::hook::Install("WmoGroupParse", wmo::kGroupParse, &hkWmoGroupParse, &g_origWmoGroup);
        return true;
    }
}

WXL_REGISTER_FEATURE("spawn", wxl::features::kSpawn, InstallSpawn)
