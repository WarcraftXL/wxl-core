// ADT method context: the wxl.adt subtable (split-map state, split reader stats, tile status).
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
#include "features/adtsplit/AdtSplit.hpp"

#include <cstdint>

/// The terrain (ADT) context, in the WorldMethods mould: one header whose Register() adds the
/// wxl.adt subtable to the `wxl` table on top of the stack. Every method reads a by-value snapshot
/// from the split-ADT reader's query surface (features/adtsplit) -- no game memory is touched here,
/// so no SEH guards are needed. Header-only and inline.
namespace wxl::lua::methods::adt
{
    namespace split = wxl::runtime::adtsplit;

    /// Pushes one TileStatus as a Lua table (shared by tile_status and tiles).
    inline void PushTileStatus(lua_State* L, const split::TileStatus& ts)
    {
        lua_newtable(L);
        lua_pushinteger(L, ts.tileFirst);                 lua_setfield(L, -2, "tile_x");
        lua_pushinteger(L, ts.tileSecond);                lua_setfield(L, -2, "tile_y");
        lua_pushinteger(L, static_cast<lua_Integer>(ts.rootSize));  lua_setfield(L, -2, "root_size");
        lua_pushinteger(L, static_cast<lua_Integer>(ts.texSize));   lua_setfield(L, -2, "tex_size");
        lua_pushinteger(L, static_cast<lua_Integer>(ts.objSize));   lua_setfield(L, -2, "obj_size");
        lua_pushinteger(L, static_cast<lua_Integer>(ts.chunkCount)); lua_setfield(L, -2, "chunks");
        lua_pushboolean(L, ts.complete ? 1 : 0);          lua_setfield(L, -2, "complete");
        lua_pushboolean(L, ts.hasMtxp ? 1 : 0);           lua_setfield(L, -2, "has_mtxp");
        lua_pushinteger(L, static_cast<lua_Integer>(ts.mclvChunks)); lua_setfield(L, -2, "mclv_chunks");
        lua_pushinteger(L, static_cast<lua_Integer>(ts.hiResHoleChunks));
        lua_setfield(L, -2, "hires_hole_chunks");
    }

    /// wxl.adt.is_split_map() -> bool|nil: true/false once the current map's split-ness is probed
    /// (first tile load), nil while unknown or out of world.
    inline int L_isSplitMap(lua_State* L)
    {
        const int state = split::IsSplitMap();
        if (state < 0)
            PushNil(L);
        else
            Push(L, state == 1);
        return 1;
    }

    /// wxl.adt.split_stats() -> table: session counters of the split reader (split_maps,
    /// tiles_loaded, tiles_resident, chunks_filled, mcrf_bytes, parked_mtxp_tiles,
    /// parked_mclv_chunks, parked_hole_chunks, load_failures).
    inline int L_splitStats(lua_State* L)
    {
        const split::Stats s = split::GetStats();
        lua_newtable(L);
        lua_pushinteger(L, static_cast<lua_Integer>(s.splitMapsDetected)); lua_setfield(L, -2, "split_maps");
        lua_pushinteger(L, static_cast<lua_Integer>(s.tilesLoaded));       lua_setfield(L, -2, "tiles_loaded");
        lua_pushinteger(L, static_cast<lua_Integer>(s.tilesResident));     lua_setfield(L, -2, "tiles_resident");
        lua_pushinteger(L, static_cast<lua_Integer>(s.chunksFilled));      lua_setfield(L, -2, "chunks_filled");
        lua_pushinteger(L, static_cast<lua_Integer>(s.mcrfBytes));         lua_setfield(L, -2, "mcrf_bytes");
        lua_pushinteger(L, static_cast<lua_Integer>(s.parkedMtxpTiles));   lua_setfield(L, -2, "parked_mtxp_tiles");
        lua_pushinteger(L, static_cast<lua_Integer>(s.parkedMclvChunks));  lua_setfield(L, -2, "parked_mclv_chunks");
        lua_pushinteger(L, static_cast<lua_Integer>(s.parkedHoleChunks));  lua_setfield(L, -2, "parked_hole_chunks");
        lua_pushinteger(L, static_cast<lua_Integer>(s.loadFailures));      lua_setfield(L, -2, "load_failures");
        return 1;
    }

    /// wxl.adt.tile_status(tileX, tileY) -> table|nil: status of one RESIDENT split tile (the two
    /// %d of the tile filename), nil when that tile is not resident or the map is not split.
    inline int L_tileStatus(lua_State* L)
    {
        const int tx = static_cast<int>(CheckInt(L, 1));
        const int ty = static_cast<int>(CheckInt(L, 2));
        split::TileStatus ts{};
        if (!split::FindTile(tx, ty, ts))
        {
            PushNil(L);
            return 1;
        }
        PushTileStatus(L, ts);
        return 1;
    }

    /// wxl.adt.tiles() -> array of tile-status tables for every resident split tile (may be empty).
    inline int L_tiles(lua_State* L)
    {
        lua_newtable(L);
        const uint32_t n = split::ResidentTileCount();
        int out = 0;
        for (uint32_t i = 0; i < n; ++i)
        {
            split::TileStatus ts{};
            if (!split::GetResidentTile(i, ts)) break;
            PushTileStatus(L, ts);
            lua_rawseti(L, -2, ++out);
        }
        return 1;
    }

    /**
     * @brief Adds the wxl.adt subtable to the table on top of the stack. Stack-neutral.
     * @param L  the engine lua_State, with the target `wxl` table at index -1.
     */
    inline void Register(lua_State* L)
    {
        static const luaL_Reg fns[] = {
            { "is_split_map", L_isSplitMap },
            { "split_stats",  L_splitStats },
            { "tile_status",  L_tileStatus },
            { "tiles",        L_tiles },
            { nullptr, nullptr },
        };
        RegisterModule(L, "adt", fns);
    }
}
