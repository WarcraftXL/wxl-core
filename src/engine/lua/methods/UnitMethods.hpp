// Unit method context: unit:* instance methods and the wxl.player/target/mouseover accessors.
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
#include "engine/lua/ObjectProxy.hpp"
#include "offsets/game/Unit.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>

/// The Unit context, in the CoreMethods mould: one header whose Register() (1) installs the
/// unit:* instance methods onto the Unit metatable created by ObjectProxy::RegisterMetatables, and
/// (2) adds the wxl.player/target/mouseover accessors to the `wxl` table on top of the stack. Every
/// instance method resolves the userdata's GUID to a fresh native pointer through ObjectProxy (SEH
/// guarded) and returns nil/false rather than trusting a stale pointer. Header-only and inline.
namespace wxl::lua::methods::unit
{
    namespace off = wxl::offsets::game::unit;

    // --- instance methods (closures on the Unit metatable) ---

    /// unit:IsValid() -> bool: does the GUID still resolve to a live object.
    inline int L_IsValid(lua_State* L)
    {
        Push(L, ResolveUnit(CheckGuid(L, 1)) != nullptr);
        return 1;
    }

    /// unit:GetGuid() -> string: the GUID as "0x<16 hex>" (a u64 does not fit a Lua number).
    inline int L_GetGuid(lua_State* L)
    {
        const uint64_t guid = CheckGuid(L, 1);
        char buf[19];
        std::snprintf(buf, sizeof(buf), "0x%016llX", static_cast<unsigned long long>(guid));
        Push(L, static_cast<const char*>(buf));
        return 1;
    }

    /// unit:GetPosition() -> x, y, z (numbers) or nil when unresolved / faulted.
    inline int L_GetPosition(lua_State* L)
    {
        void* unit = ResolveUnit(CheckGuid(L, 1));
        float pos[3];
        if (!ReadPosition(unit, pos))
        {
            PushNil(L);
            return 1;
        }
        Push(L, static_cast<double>(pos[0]));
        Push(L, static_cast<double>(pos[1]));
        Push(L, static_cast<double>(pos[2]));
        return 3;
    }

    /// unit:IsPlayer() -> bool: does the GUID resolve under the player type mask.
    inline int L_IsPlayer(lua_State* L)
    {
        Push(L, ResolveObject(CheckGuid(L, 1), off::kTypeMaskPlayer) != nullptr);
        return 1;
    }

    /// unit:GetReaction(other) -> int (0..1 hostile, 2..3 neutral, 4+ friendly) or nil.
    inline int L_GetReaction(lua_State* L)
    {
        void* self  = ResolveUnit(CheckGuid(L, 1));
        void* other = ResolveUnit(CheckGuid(L, 2));
        int   reaction;
        if (!ReadReaction(self, other, &reaction))
        {
            PushNil(L);
            return 1;
        }
        Push(L, static_cast<lua_Integer>(reaction));
        return 1;
    }

    /// unit:DistanceTo(other) -> number: straight-line distance between the two units, or nil when
    /// either position is unavailable.
    inline int L_DistanceTo(lua_State* L)
    {
        void* self  = ResolveUnit(CheckGuid(L, 1));
        void* other = ResolveUnit(CheckGuid(L, 2));
        float a[3], b[3];
        if (!ReadPosition(self, a) || !ReadPosition(other, b))
        {
            PushNil(L);
            return 1;
        }
        const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
        Push(L, static_cast<double>(std::sqrt(dx * dx + dy * dy + dz * dz)));
        return 1;
    }

    /// unit:GetScreenPosition() -> screenX, screenY, visible: the unit's world position projected to
    /// ImGui pixel space (shared with wxl.camera.world_to_screen). nil when the unit has no position
    /// or the projection is unavailable.
    inline int L_GetScreenPosition(lua_State* L)
    {
        void* unit = ResolveUnit(CheckGuid(L, 1));
        float pos[3];
        if (!ReadPosition(unit, pos))
        {
            PushNil(L);
            return 1;
        }
        float sx = 0.0f, sy = 0.0f;
        bool  visible = false;
        if (!WorldToScreenPixels(pos, sx, sy, visible))
        {
            PushNil(L);
            return 1;
        }
        Push(L, static_cast<double>(sx));
        Push(L, static_cast<double>(sy));
        Push(L, visible);
        return 3;
    }

    // --- global accessors (fields on the wxl table) ---

    /// wxl.player() -> Unit for the active player, or nil.
    inline int L_player(lua_State* L)
    {
        PushUnit(L, ActivePlayerGuid());
        return 1;
    }

    /// wxl.target() -> Unit for the current target, or nil.
    inline int L_target(lua_State* L)
    {
        PushUnit(L, ReadGuidAt(off::kTargetGuid));
        return 1;
    }

    /// wxl.mouseover() -> Unit under the cursor, or nil.
    inline int L_mouseover(lua_State* L)
    {
        PushUnit(L, ReadGuidAt(off::kMouseoverGuid));
        return 1;
    }

    /**
     * @brief Installs the Unit instance methods onto the metatable and adds the global accessors to
     *        the table on top of the stack. Requires RegisterMetatables to have run first.
     * @param L  the engine lua_State, with the target `wxl` table at index -1.
     */
    inline void Register(lua_State* L)
    {
        // Instance methods into mt.__index (created by ObjectProxy::RegisterMetatables).
        luaL_getmetatable(L, kUnitMeta);                                     // [wxl, mt]
        lua_getfield(L, -1, "__index");                                      // [wxl, mt, methods]
        lua_pushcfunction(L, &L_IsValid);          lua_setfield(L, -2, "IsValid");
        lua_pushcfunction(L, &L_GetGuid);          lua_setfield(L, -2, "GetGuid");
        lua_pushcfunction(L, &L_GetPosition);      lua_setfield(L, -2, "GetPosition");
        lua_pushcfunction(L, &L_IsPlayer);         lua_setfield(L, -2, "IsPlayer");
        lua_pushcfunction(L, &L_GetReaction);      lua_setfield(L, -2, "GetReaction");
        lua_pushcfunction(L, &L_DistanceTo);       lua_setfield(L, -2, "DistanceTo");
        lua_pushcfunction(L, &L_GetScreenPosition);lua_setfield(L, -2, "GetScreenPosition");
        lua_pop(L, 2);                                                       // [wxl]

        // Global accessors onto the wxl table.
        lua_pushcfunction(L, &L_player);    lua_setfield(L, -2, "player");
        lua_pushcfunction(L, &L_target);    lua_setfield(L, -2, "target");
        lua_pushcfunction(L, &L_mouseover); lua_setfield(L, -2, "mouseover");
    }
}
