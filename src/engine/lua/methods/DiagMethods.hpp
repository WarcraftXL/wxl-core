// wxl.diag.*: frame draw-call accounting and M2 doodad-batching diagnosis, surfaced to Lua.
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

#include "config.hpp"
#include "engine/lua/LuaJit.hpp"
#include "engine/lua/Marshal.hpp"
#include "features/diag/DrawStats.hpp"

/// Register() adds the wxl.diag subtable to the `wxl` table on the stack. Read-only counters plus a
/// reset, so a script can measure one zone, walk to another, and compare -- which is the whole point:
/// the numbers only mean something when you can bracket a specific place with them.
namespace wxl::lua::methods::diag
{
    /// wxl.diag.enabled() -> bool: the diagnostics feature is compiled in (config.hpp kDiag). When
    /// false every counter below stays zero because the hot-path hooks were compiled out.
    inline int L_enabled(lua_State* L)
    {
        Push(L, wxl::features::kDiag);
        return 1;
    }

    /// wxl.diag.draw_stats() -> table: per-frame device load and the M2 batching verdict.
    ///   draws / m2_draws / tris          -- the most recently completed frame
    ///   peak_draws / peak_m2_draws / peak_tris
    ///   avg_draws / avg_m2_draws         -- means over frames_sampled
    ///   frames_sampled                   -- frames that issued at least one draw
    ///   models_finalized                 -- skin finalizes observed
    ///   models_non_batchable             -- of those, max-instances < 2, so the client cleared their
    ///                                       "batchable doodad" flag: one draw call per placement
    ///   min_max_instances                -- smallest max-instances seen (nil when nothing finalized)
    inline int L_drawStats(lua_State* L)
    {
        const auto s = wxl::runtime::drawstats::Get();
        lua_newtable(L);
        lua_pushinteger(L, static_cast<lua_Integer>(s.last.draws));        lua_setfield(L, -2, "draws");
        lua_pushinteger(L, static_cast<lua_Integer>(s.last.m2Draws));      lua_setfield(L, -2, "m2_draws");
        lua_pushinteger(L, static_cast<lua_Integer>(s.last.tris));         lua_setfield(L, -2, "tris");
        lua_pushinteger(L, static_cast<lua_Integer>(s.peak.draws));        lua_setfield(L, -2, "peak_draws");
        lua_pushinteger(L, static_cast<lua_Integer>(s.peak.m2Draws));      lua_setfield(L, -2, "peak_m2_draws");
        lua_pushinteger(L, static_cast<lua_Integer>(s.peak.tris));         lua_setfield(L, -2, "peak_tris");
        lua_pushnumber (L, s.avgDraws);                                    lua_setfield(L, -2, "avg_draws");
        lua_pushnumber (L, s.avgM2Draws);                                  lua_setfield(L, -2, "avg_m2_draws");
        lua_pushinteger(L, static_cast<lua_Integer>(s.framesSampled));     lua_setfield(L, -2, "frames_sampled");
        lua_pushinteger(L, static_cast<lua_Integer>(s.modelsFinalized));   lua_setfield(L, -2, "models_finalized");
        lua_pushinteger(L, static_cast<lua_Integer>(s.modelsNonBatchable));lua_setfield(L, -2, "models_non_batchable");
        if (s.minMaxInstances == 0xFFFFFFFFu) PushNil(L);
        else lua_pushinteger(L, static_cast<lua_Integer>(s.minMaxInstances));
        lua_setfield(L, -2, "min_max_instances");
        return 1;
    }

    /// wxl.diag.reset(): clears peaks, averages and the batching tallies. Call it on arrival in a zone
    /// so the numbers describe that zone and not the whole session.
    inline int L_reset(lua_State* L)
    {
        (void)L;
        wxl::runtime::drawstats::Reset();
        return 0;
    }

    /**
     * @brief Adds the wxl.diag subtable to the table on top of the stack. Stack-neutral.
     * @param L  the engine lua_State, with the target `wxl` table at index -1.
     */
    inline void Register(lua_State* L)
    {
        static const luaL_Reg fns[] = {
            { "enabled",    L_enabled },
            { "draw_stats", L_drawStats },
            { "reset",      L_reset },
            { nullptr, nullptr },
        };
        RegisterModule(L, "diag", fns);
    }
}
