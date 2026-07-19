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
#include "features/adtsplit/HeightBlend.hpp"

#include <cstdint>

/// The terrain (ADT) context, in the WorldMethods mould: one header whose Register() adds the
/// wxl.adt subtable to the `wxl` table on top of the stack. Every method reads a by-value snapshot
/// from the split-ADT reader's query surface (features/adtsplit) -- no game memory is touched here,
/// so no SEH guards are needed. Header-only and inline.
namespace wxl::lua::methods::adt
{
    namespace split = wxl::runtime::adtsplit;
    namespace hb    = wxl::features::heightblend;

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
    /// parked_mclv_chunks, parked_hole_chunks, load_failures, wdl_read).
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
        lua_pushinteger(L, static_cast<lua_Integer>(s.wdlRead));           lua_setfield(L, -2, "wdl_read");
        lua_pushinteger(L, static_cast<lua_Integer>(s.heightTexLoaded));   lua_setfield(L, -2, "height_tex_loaded");
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

    // --- height blend (Legion MTXP "_h" terrain texture blending) ---

    /// wxl.adt.height_blend_installed() -> bool: the terrain draw detour is live.
    inline int L_hbInstalled(lua_State* L) { Push(L, hb::Installed()); return 1; }

    /// wxl.adt.height_blend_enabled() -> bool / wxl.adt.set_height_blend_enabled(bool): runtime
    /// master switch; off = every terrain chunk draws through the untouched stock path.
    inline int L_hbEnabled(lua_State* L) { Push(L, hb::Get().enabled); return 1; }
    inline int L_hbSetEnabled(lua_State* L) { hb::Get().enabled = CheckBool(L, 1); return 0; }

    /// wxl.adt.height_sharpness() -> number / wxl.adt.set_height_sharpness(number): the sharpen
    /// strength in w *= 1 - sat((max(w) - w) * sharpness). 0 = pure MAD+normalize blend,
    /// 1 = the community-disassembled Legion form. Clamped to [0, 16]; applies next frame.
    inline int L_hbSharpness(lua_State* L) { Push(L, static_cast<double>(hb::Get().sharpness)); return 1; }
    inline int L_hbSetSharpness(lua_State* L)
    {
        double v = CheckNumber(L, 1);
        if (v < 0.0) v = 0.0;
        if (v > 16.0) v = 16.0;
        hb::Get().sharpness = static_cast<float>(v);
        return 0;
    }

    /// wxl.adt.height_channel() -> "a"|"r" / wxl.adt.set_height_channel("a"|"r"): which channel of
    /// the "_h" texture carries the height (alpha is the Legion layout; red is the fallback for
    /// alpha-less DXT1 repacks). Changing it rebuilds the patched shaders on next draw.
    inline int L_hbChannel(lua_State* L) { Push(L, hb::Get().channelRed ? "r" : "a"); return 1; }
    inline int L_hbSetChannel(lua_State* L)
    {
        const char* s = CheckString(L, 1);
        const bool red = s && (s[0] == 'r' || s[0] == 'R');
        if (red != hb::Get().channelRed)
        {
            hb::Get().channelRed = red;
            hb::InvalidateShaders();
        }
        return 0;
    }

    /// wxl.adt.height_blend_stats() -> table: {patched_shaders, patch_failures, chunks_drawn,
    /// active} session counters of the height-blend draw.
    inline int L_hbStats(lua_State* L)
    {
        const hb::Stats s = hb::GetStats();
        lua_newtable(L);
        lua_pushinteger(L, static_cast<lua_Integer>(s.patchedShaders)); lua_setfield(L, -2, "patched_shaders");
        lua_pushinteger(L, static_cast<lua_Integer>(s.patchFailures));  lua_setfield(L, -2, "patch_failures");
        lua_pushinteger(L, static_cast<lua_Integer>(s.chunksDrawn));    lua_setfield(L, -2, "chunks_drawn");
        lua_pushboolean(L, s.active ? 1 : 0);                           lua_setfield(L, -2, "active");
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
            { "height_blend_installed",   L_hbInstalled },
            { "height_blend_enabled",     L_hbEnabled },
            { "set_height_blend_enabled", L_hbSetEnabled },
            { "height_sharpness",         L_hbSharpness },
            { "set_height_sharpness",     L_hbSetSharpness },
            { "height_channel",           L_hbChannel },
            { "set_height_channel",       L_hbSetChannel },
            { "height_blend_stats",       L_hbStats },
            { nullptr, nullptr },
        };
        RegisterModule(L, "adt", fns);
    }
}
