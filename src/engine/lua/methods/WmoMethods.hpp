// WMO method context: the wxl.wmo subtable reading a map-object root/group event pointer's fields.
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
#include "game/wmo/Wmo.hpp"
#include "offsets/game/WMO.hpp"

#include <windows.h>

/// The WMO context, in the CameraMethods mould: Register() adds the wxl.wmo subtable to the `wxl` table
/// on the stack. The root fields take the pointer a "wmo_root_load" handler received as LIGHTUSERDATA;
/// the group field takes a "wmo_group_load" pointer. Each read is an SEH-guarded POD copy off a confirmed
/// offset, because the game accessors here only null-check (they do not range-guard an arbitrary event
/// pointer). interior_path() takes no argument: it reads the map-object the camera is currently inside.
/// Header-only and inline.
namespace wxl::lua::methods::wmo
{
    namespace gw  = wxl::game::wmo;
    namespace off = wxl::offsets::game::wmo;

    /// The root inline path buffer is bounded by the client; cap our copy conservatively.
    inline constexpr int kNameCap = 256;

    /// wxl.wmo.name(root) -> string or nil (inline map-object path at root+kOffNameInline). root is a
    /// wmo_root_load event lightuserdata.
    inline int L_name(lua_State* L)
    {
        void* root = CheckPtr(L, 1);
        char buf[kNameCap];
        if (!root || SehReadCStr(root, off::kOffNameInline, buf, kNameCap) == 0)
        {
            PushNil(L);
            return 1;
        }
        Push(L, static_cast<const char*>(buf));
        return 1;
    }

    /// wxl.wmo.root_size(root) -> int or nil (root buffer byte size). root is a wmo_root_load event
    /// lightuserdata.
    inline int L_rootSize(lua_State* L)
    {
        void* root = CheckPtr(L, 1);
        uint32_t v = 0;
        if (!root || !SehReadU32(root, off::kOffRootSize, v))
        {
            PushNil(L);
            return 1;
        }
        Push(L, static_cast<lua_Integer>(v));
        return 1;
    }

    /// wxl.wmo.group_count(root) -> int or nil (number of groups). root is a wmo_root_load event
    /// lightuserdata.
    inline int L_groupCount(lua_State* L)
    {
        void* root = CheckPtr(L, 1);
        uint32_t v = 0;
        if (!root || !SehReadU32(root, off::kOffGroupCount, v))
        {
            PushNil(L);
            return 1;
        }
        Push(L, static_cast<lua_Integer>(v));
        return 1;
    }

    /// wxl.wmo.material_count(root) -> int or nil (number of materials). root is a wmo_root_load event
    /// lightuserdata.
    inline int L_materialCount(lua_State* L)
    {
        void* root = CheckPtr(L, 1);
        uint32_t v = 0;
        if (!root || !SehReadU32(root, off::kOffMaterialCount, v))
        {
            PushNil(L);
            return 1;
        }
        Push(L, static_cast<lua_Integer>(v));
        return 1;
    }

    /// wxl.wmo.group_size(group) -> int or nil (group buffer byte size). group is a wmo_group_load event
    /// lightuserdata.
    inline int L_groupSize(lua_State* L)
    {
        void* group = CheckPtr(L, 1);
        uint32_t v = 0;
        if (!group || !SehReadU32(group, off::kOffGroupSize, v))
        {
            PushNil(L);
            return 1;
        }
        Push(L, static_cast<lua_Integer>(v));
        return 1;
    }

    /// wxl.wmo.interior_path() -> string or nil: the inline path of the map-object the camera is currently
    /// inside (nil outdoors). Reads a live engine global; no argument.
    inline int L_interiorPath(lua_State* L)
    {
        const char* p = nullptr;
        __try
        {
            p = gw::CurrentInteriorPath();
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            p = nullptr;
        }
        char buf[kNameCap];
        if (!p || SehReadCStr(p, 0, buf, kNameCap) == 0)
        {
            PushNil(L);
            return 1;
        }
        Push(L, static_cast<const char*>(buf));
        return 1;
    }

    /**
     * @brief Adds the wxl.wmo subtable to the table on top of the stack. Stack-neutral.
     * @param L  the engine lua_State, with the target `wxl` table at index -1.
     */
    inline void Register(lua_State* L)
    {
        lua_newtable(L);                                                            // [wxl, wmo]
        lua_pushcfunction(L, &L_name);          lua_setfield(L, -2, "name");
        lua_pushcfunction(L, &L_rootSize);      lua_setfield(L, -2, "root_size");
        lua_pushcfunction(L, &L_groupCount);    lua_setfield(L, -2, "group_count");
        lua_pushcfunction(L, &L_materialCount); lua_setfield(L, -2, "material_count");
        lua_pushcfunction(L, &L_groupSize);     lua_setfield(L, -2, "group_size");
        lua_pushcfunction(L, &L_interiorPath);  lua_setfield(L, -2, "interior_path");
        lua_setfield(L, -2, "wmo");                                                 // [wxl]
    }
}
