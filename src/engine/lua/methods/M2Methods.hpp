// M2 method context: the wxl.m2 subtable reading a model/instance event pointer's identity fields.
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

#pragma once

#include "engine/lua/LuaJit.hpp"
#include "engine/lua/Marshal.hpp"
#include "engine/lua/methods/PointerArg.hpp"
#include "features/m2compat/ShadowSpace.hpp"
#include "features/m2native/M2Native.hpp"
#include "features/m2native/ParticleStride.hpp"
#include "game/m2/M2.hpp"
#include "offsets/game/M2.hpp"

/// The M2 context, in the CameraMethods mould: Register() adds the wxl.m2 subtable to the `wxl` table on
/// the stack. The model fields take the runtime-model pointer a "model_load" / "model_load_pre" /
/// "m2_skin_finalize" handler received as LIGHTUSERDATA; instance_model takes the render-context pointer a
/// "m2_update" / "bone_palette" handler received. Each field is an SEH-guarded POD read off a confirmed
/// M2 object offset, so a stale or wrong-typed event pointer yields nil rather than faulting. Header-only
/// and inline.
namespace wxl::lua::methods::m2
{
    namespace off = wxl::offsets::game::m2;

    /// The model path-stem inline buffer is bounded by the client; cap our copy conservatively.
    inline constexpr int kStemCap = 256;

    /// wxl.m2.path_stem(model) -> string or nil (inline path stem, no extension). model is a model_load /
    /// model_load_pre / m2_skin_finalize event lightuserdata.
    inline int L_pathStem(lua_State* L)
    {
        void* m = CheckPtr(L, 1);
        char buf[kStemCap];
        if (!m || SehReadCStr(m, off::kOffModelPathStem, buf, kStemCap) == 0)
        {
            PushNil(L);
            return 1;
        }
        Push(L, static_cast<const char*>(buf));
        return 1;
    }

    /// wxl.m2.file_size(model) -> int or nil (byte size of the parsed .m2 buffer). model is a model_load
    /// event lightuserdata.
    inline int L_fileSize(lua_State* L)
    {
        void* m = CheckPtr(L, 1);
        uint32_t v = 0;
        if (!m || !SehReadU32(m, off::kOffModelFileSize, v))
        {
            PushNil(L);
            return 1;
        }
        Push(L, static_cast<lua_Integer>(v));
        return 1;
    }

    /// wxl.m2.flags(model) -> int or nil (runtime model flags; bit 2 selects the sibling-file open flag).
    /// model is a model_load event lightuserdata.
    inline int L_flags(lua_State* L)
    {
        void* m = CheckPtr(L, 1);
        uint32_t v = 0;
        if (!m || !SehReadU32(m, off::kOffModelFlags, v))
        {
            PushNil(L);
            return 1;
        }
        Push(L, static_cast<lua_Integer>(v));
        return 1;
    }

    /// wxl.m2.instance_model(render_ctx) -> lightuserdata or nil: the runtime model an instance draws
    /// (render_ctx+kOffInstModel). render_ctx is an m2_update / bone_palette event lightuserdata; feed the
    /// result back into wxl.m2.path_stem / file_size / flags.
    inline int L_instanceModel(lua_State* L)
    {
        void* inst = CheckPtr(L, 1);
        void* model = inst ? SehReadPtr(inst, off::kOffInstModel) : nullptr;
        if (!model)
        {
            PushNil(L);
            return 1;
        }
        lua_pushlightuserdata(L, model);
        return 1;
    }

    // --- native MD21 reader (features/m2native) ---

    /// wxl.m2.native_enabled() -> bool: the native modern-M2 (MD21) reader is compiled in.
    inline int L_nativeEnabled(lua_State* L)
    {
        Push(L, wxl::runtime::m2native::Enabled());
        return 1;
    }

    /// wxl.m2.native_stats() -> table: session counters of the native MD21 reader
    /// (models_native, models_failed, textures_resolved, textures_unresolved, skipped_cameras,
    /// skipped_particles, skipped_txac, skipped_ldv1, skipped_afid, skipped_skid,
    /// skipped_other_chunks, external_seq_pending).
    inline int L_nativeStats(lua_State* L)
    {
        const auto s = wxl::runtime::m2native::GetStats();
        lua_newtable(L);
        lua_pushinteger(L, static_cast<lua_Integer>(s.modelsNative));       lua_setfield(L, -2, "models_native");
        lua_pushinteger(L, static_cast<lua_Integer>(s.modelsFailed));       lua_setfield(L, -2, "models_failed");
        lua_pushinteger(L, static_cast<lua_Integer>(s.texturesResolved));   lua_setfield(L, -2, "textures_resolved");
        lua_pushinteger(L, static_cast<lua_Integer>(s.texturesUnresolved)); lua_setfield(L, -2, "textures_unresolved");
        lua_pushinteger(L, static_cast<lua_Integer>(s.skippedCameras));     lua_setfield(L, -2, "skipped_cameras");
        lua_pushinteger(L, static_cast<lua_Integer>(s.skippedParticles));   lua_setfield(L, -2, "skipped_particles");
        lua_pushinteger(L, static_cast<lua_Integer>(s.skippedTxac));        lua_setfield(L, -2, "skipped_txac");
        lua_pushinteger(L, static_cast<lua_Integer>(s.skippedLdv1));        lua_setfield(L, -2, "skipped_ldv1");
        lua_pushinteger(L, static_cast<lua_Integer>(s.skippedAfid));        lua_setfield(L, -2, "skipped_afid");
        lua_pushinteger(L, static_cast<lua_Integer>(s.skippedSkid));        lua_setfield(L, -2, "skipped_skid");
        lua_pushinteger(L, static_cast<lua_Integer>(s.skippedOtherChunks)); lua_setfield(L, -2, "skipped_other_chunks");
        lua_pushinteger(L, static_cast<lua_Integer>(s.externalSeqPending)); lua_setfield(L, -2, "external_seq_pending");
        lua_pushinteger(L, static_cast<lua_Integer>(s.shadowGateForced));   lua_setfield(L, -2, "shadow_gate_forced");
        return 1;
    }

    // --- ground-shadow space fix (features/m2compat/ShadowSpace) ---

    /// wxl.m2.shadow_fix_installed() -> bool: the shadow-batch detour is compiled in and hooked.
    inline int L_shadowFixInstalled(lua_State* L)
    {
        Push(L, wxl::runtime::m2shadow::Installed());
        return 1;
    }

    /// wxl.m2.shadow_fix_enabled() -> bool: whether the shadow draw currently runs with a model-space
    /// bone palette (true) or exactly as stock, view-space palette and all (false).
    inline int L_shadowFixEnabled(lua_State* L)
    {
        Push(L, wxl::runtime::m2shadow::Enabled());
        return 1;
    }

    /// wxl.m2.set_shadow_fix_enabled(bool): flips the fix live, for A/B against stock. Turn it off,
    /// rotate the camera, and a prop shadow that starts swinging is the bug this fixes.
    inline int L_setShadowFixEnabled(lua_State* L)
    {
        wxl::runtime::m2shadow::SetEnabled(CheckBool(L, 1));
        return 0;
    }

    /// wxl.m2.shadow_fix_stats() -> table: shadow_draws, models_logged, bones_seen, bones_billboard,
    /// bones_transformed, faults. bones_billboard > 0 supports the billboard hypothesis for the
    /// residual shadow swing; bones_billboard == 0 with a visibly swinging shadow refutes it.
    inline int L_shadowFixStats(lua_State* L)
    {
        const auto s = wxl::runtime::m2shadow::GetStats();
        lua_newtable(L);
        lua_pushinteger(L, static_cast<lua_Integer>(s.shadowDraws));      lua_setfield(L, -2, "shadow_draws");
        lua_pushinteger(L, static_cast<lua_Integer>(s.pairsLogged));      lua_setfield(L, -2, "pairs_logged");
        lua_pushinteger(L, static_cast<lua_Integer>(s.influencesZero));   lua_setfield(L, -2, "influences_zero");
        lua_pushinteger(L, static_cast<lua_Integer>(s.influencesFixed));  lua_setfield(L, -2, "influences_fixed");
        lua_pushinteger(L, static_cast<lua_Integer>(s.overrideSections)); lua_setfield(L, -2, "override_sections");
        lua_pushinteger(L, static_cast<lua_Integer>(s.staleAnim));        lua_setfield(L, -2, "stale_anim");
        lua_pushinteger(L, static_cast<lua_Integer>(s.paletteMismatch));  lua_setfield(L, -2, "palette_mismatch");
        lua_pushinteger(L, static_cast<lua_Integer>(s.faults));           lua_setfield(L, -2, "faults");
        return 1;
    }

    // --- modern particle emitters (features/m2native/ParticleStride) ---

    /// wxl.m2.particles_installed() -> bool: the client was taught the modern emitter stride and can
    /// read modern particles in place (all nine stride sites patched). When false, modern models'
    /// emitters are parked and show no fire/smoke.
    inline int L_particlesInstalled(lua_State* L)
    {
        Push(L, wxl::runtime::m2particles::Installed());
        return 1;
    }

    /// wxl.m2.set_particle_zsource_fix(bool): flips the zSource=255 sentinel rewrite live, for A/B.
    /// With it off, modern fire drifts sideways (the client reads 255 as a real point source).
    inline int L_setParticleZSourceFix(lua_State* L)
    {
        wxl::runtime::m2particles::SetZSourceFixEnabled(CheckBool(L, 1));
        return 0;
    }

    /// wxl.m2.particle_stats() -> table: sites_patched (9 when active), zsource_neutralized (running
    /// count of emitters whose 255 sentinel was rewritten to 0), zsource_fix_enabled.
    inline int L_particleStats(lua_State* L)
    {
        const auto s = wxl::runtime::m2particles::GetStats();
        lua_newtable(L);
        lua_pushinteger(L, static_cast<lua_Integer>(s.sitesPatched));       lua_setfield(L, -2, "sites_patched");
        lua_pushinteger(L, static_cast<lua_Integer>(s.zSourceNeutralized)); lua_setfield(L, -2, "zsource_neutralized");
        Push(L, wxl::runtime::m2particles::ZSourceFixEnabled());            lua_setfield(L, -2, "zsource_fix_enabled");
        return 1;
    }

    /// wxl.m2.load(path) -> lightuserdata or nil: loads an .m2 by virtual path through the stock
    /// resource loader (the same path the client's attachments use), driving the full load chain --
    /// for an MD21 file, the native reader. The returned handle stays alive until wxl.m2.unload;
    /// primarily a smoke-test hook (load succeeds / native_stats moves / no crash).
    inline int L_load(lua_State* L)
    {
        const char* path = CheckString(L, 1);
        void* handle = path && *path ? wxl::game::m2::LoadResource(path, 0) : nullptr;
        if (!handle)
        {
            PushNil(L);
            return 1;
        }
        lua_pushlightuserdata(L, handle);
        return 1;
    }

    /// wxl.m2.unload(handle): releases a handle returned by wxl.m2.load. Passing anything else is
    /// undefined -- dev/test surface only.
    inline int L_unload(lua_State* L)
    {
        if (void* handle = CheckPtr(L, 1))
            wxl::game::m2::ReleaseResource(handle);
        return 0;
    }

    /**
     * @brief Adds the wxl.m2 subtable to the table on top of the stack. Stack-neutral.
     * @param L  the engine lua_State, with the target `wxl` table at index -1.
     */
    inline void Register(lua_State* L)
    {
        static const luaL_Reg fns[] = {
            { "path_stem",      L_pathStem },
            { "file_size",      L_fileSize },
            { "flags",          L_flags },
            { "instance_model", L_instanceModel },
            { "native_enabled", L_nativeEnabled },
            { "native_stats",   L_nativeStats },
            { "shadow_fix_installed",   L_shadowFixInstalled },
            { "shadow_fix_enabled",     L_shadowFixEnabled },
            { "set_shadow_fix_enabled", L_setShadowFixEnabled },
            { "shadow_fix_stats",       L_shadowFixStats },
            { "particles_installed",     L_particlesInstalled },
            { "set_particle_zsource_fix",L_setParticleZSourceFix },
            { "particle_stats",          L_particleStats },
            { "load",           L_load },
            { "unload",         L_unload },
            { nullptr, nullptr },
        };
        RegisterModule(L, "m2", fns);
    }
}
