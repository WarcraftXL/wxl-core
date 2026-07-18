// World-pick method context: adds wxl.world.pick / focus_position onto the existing wxl.world subtable.
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
#include "game/world/Pick.hpp"
#include "offsets/game/World.hpp"

#include <windows.h>

/// A second world context that extends the wxl.world subtable already built by WorldMethods::Register
/// with two reads that context does not cover: the cursor world pick and the focus world position. Its
/// Register() fetches the existing "world" subtable off the `wxl` table and adds the fields, so it must
/// run AFTER methods::world::Register(L). The pick goes through the proven wxl::game::world::PickCursor
/// wrapper (the same call the native cursor selection and the world_click event use); the focus position
/// is an SEH-guarded POD read of the confirmed load-box-center globals. Header-only and inline.
namespace wxl::lua::methods::worldpick
{
    namespace gw   = wxl::game::world;
    namespace woff = wxl::offsets::game::world;

    /// wxl.world.pick() -> x, y, z, hitType or nil: casts the engine's own ray through the live cursor and
    /// reports the world hit (hitType 2 = M2/doodad, 3 = terrain/WMO). nil on a miss or out of world.
    inline int L_pick(lua_State* L)
    {
        gw::WorldHit hit;
        int type = 0;
        __try
        {
            type = gw::PickCursor(hit);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            type = 0;
        }
        if (type == 0)
        {
            PushNil(L);
            return 1;
        }
        Push(L, static_cast<double>(hit.pos.x));
        Push(L, static_cast<double>(hit.pos.y));
        Push(L, static_cast<double>(hit.pos.z));
        Push(L, static_cast<lua_Integer>(type));
        return 4;
    }

    /// wxl.world.focus_position() -> x, y, z or nil: the center of the load box (the player position the
    /// streamer keys off). Reads the live engine globals, SEH-guarded.
    inline int L_focusPosition(lua_State* L)
    {
        float p[3];
        // kFocusPosX / Y / Z are contiguous floats; read all three from the base address.
        if (!SehReadFloats(reinterpret_cast<const void*>(woff::kFocusPosX), 0, p, 3))
        {
            PushNil(L);
            return 1;
        }
        Push(L, static_cast<double>(p[0]));
        Push(L, static_cast<double>(p[1]));
        Push(L, static_cast<double>(p[2]));
        return 3;
    }

    /**
     * @brief Adds pick / focus_position onto the existing wxl.world subtable. Stack-neutral. Must run
     *        after methods::world::Register(L) has created the "world" subtable.
     * @param L  the engine lua_State, with the `wxl` table at index -1.
     */
    inline void Register(lua_State* L)
    {
        lua_getfield(L, -1, "world");                                              // [wxl, world]
        lua_pushcfunction(L, &L_pick);          lua_setfield(L, -2, "pick");
        lua_pushcfunction(L, &L_focusPosition); lua_setfield(L, -2, "focus_position");
        lua_pop(L, 1);                                                             // [wxl]
    }
}
