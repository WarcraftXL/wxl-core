// World method context: the wxl.world subtable (map id/name/dir, load state, viewport).
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
#include "game/gx/Gx.hpp"
#include "offsets/game/World.hpp"

#include <windows.h>

#include <cstdint>

/// The world context, in the CoreMethods mould: one header whose Register() adds the wxl.world
/// subtable to the `wxl` table on top of the stack. Every field reads a live engine world global
/// (map id, the loader name/dir string buffers) or the live render resolution, each guarded by the
/// same SEH/POD pattern as ObjectProxy so an out-of-world read yields nil/-1, never a fault.
/// Header-only and inline.
namespace wxl::lua::methods::world
{
    namespace woff = wxl::offsets::game::world;

    /// The loader string buffers are capped at 0x104 by the client; cap our copy conservatively.
    inline constexpr int kMapStringCap = 128;

    /// SEH-guarded read of the int32 current-map id (-1 = none / on fault).
    inline int ReadMapId()
    {
        __try
        {
            return *reinterpret_cast<const int32_t*>(woff::kCurrentMapId);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return -1;
        }
    }

    /// SEH-guarded copy of a client string buffer (fixed address) into dst, capped and null-terminated.
    /// Returns the copied length (0 when empty or on fault).
    inline int ReadMapString(uintptr_t addr, char* dst, int cap)
    {
        __try
        {
            const char* src = reinterpret_cast<const char*>(addr);
            int i = 0;
            for (; i < cap - 1 && src[i] != '\0'; ++i)
                dst[i] = src[i];
            dst[i] = '\0';
            return i;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            dst[0] = '\0';
            return 0;
        }
    }

    /// SEH-guarded live render resolution via the D3D9 device viewport (false when no device).
    inline bool ReadViewport(int& width, int& height)
    {
        __try
        {
            wxl::game::gx::Device9 dev = wxl::game::gx::Device();
            if (!dev)
                return false;
            // D3DVIEWPORT9: {X, Y, Width, Height (u32 each), MinZ, MaxZ (f32)} = 24 bytes.
            uint32_t vp[6] = {};
            dev.GetViewport(vp);
            width  = static_cast<int>(vp[2]);
            height = static_cast<int>(vp[3]);
            return width > 0 && height > 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    /// wxl.world.map_id() -> int: numeric id of the loaded map, -1 when none.
    inline int L_mapId(lua_State* L)
    {
        Push(L, static_cast<lua_Integer>(ReadMapId()));
        return 1;
    }

    /// wxl.world.is_loaded() -> bool: true while a map is loaded (map_id ~= -1).
    inline int L_isLoaded(lua_State* L)
    {
        Push(L, ReadMapId() != -1);
        return 1;
    }

    /// wxl.world.map_name() -> string|nil: the bare map name (loader global), nil out-of-world.
    inline int L_mapName(lua_State* L)
    {
        char buf[kMapStringCap];
        if (ReadMapString(woff::kMapNameStr, buf, kMapStringCap) == 0)
        {
            PushNil(L);
            return 1;
        }
        Push(L, static_cast<const char*>(buf));
        return 1;
    }

    /// wxl.world.map_dir() -> string|nil: the "World\Maps\<Map>" dir (loader global), nil out-of-world.
    inline int L_mapDir(lua_State* L)
    {
        char buf[kMapStringCap];
        if (ReadMapString(woff::kMapDirStr, buf, kMapStringCap) == 0)
        {
            PushNil(L);
            return 1;
        }
        Push(L, static_cast<const char*>(buf));
        return 1;
    }

    /// wxl.world.viewport() -> width, height (live render resolution) or nil when graphics is down.
    inline int L_viewport(lua_State* L)
    {
        int w = 0, h = 0;
        if (!ReadViewport(w, h))
        {
            PushNil(L);
            return 1;
        }
        Push(L, static_cast<lua_Integer>(w));
        Push(L, static_cast<lua_Integer>(h));
        return 2;
    }

    /**
     * @brief Adds the wxl.world subtable to the table on top of the stack. Stack-neutral.
     * @param L  the engine lua_State, with the target `wxl` table at index -1.
     */
    inline void Register(lua_State* L)
    {
        lua_newtable(L);                                                  // [wxl, world]
        lua_pushcfunction(L, &L_mapId);     lua_setfield(L, -2, "map_id");
        lua_pushcfunction(L, &L_isLoaded);  lua_setfield(L, -2, "is_loaded");
        lua_pushcfunction(L, &L_mapName);   lua_setfield(L, -2, "map_name");
        lua_pushcfunction(L, &L_mapDir);    lua_setfield(L, -2, "map_dir");
        lua_pushcfunction(L, &L_viewport);  lua_setfield(L, -2, "viewport");
        lua_setfield(L, -2, "world");                                     // [wxl]
    }
}
