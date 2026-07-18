// Doodad method context: the wxl.doodad subtable reading a placed-doodad event pointer's transform.
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
#include "game/doodad/Doodad.hpp"
#include "offsets/game/Doodad.hpp"

/// The doodad context, in the CameraMethods mould: Register() adds the wxl.doodad subtable to the `wxl`
/// table on the stack. Each field takes the placed-doodad pointer a "doodad_spawn" handler received as
/// LIGHTUSERDATA and reads its live placement. Every read goes through the existing wxl::game::doodad
/// accessors, which range-guard each dereference with VirtualQuery, so an arbitrary or stale pointer
/// degrades to nil rather than faulting. The one field with no accessor (the placement flags) is read
/// with an SEH-guarded POD copy off the confirmed offset. Header-only and inline.
namespace wxl::lua::methods::doodad
{
    namespace gd  = wxl::game::doodad;
    namespace off = wxl::offsets::game::doodad;

    /// Pushes a float[16] as a 1-based Lua array of 16 numbers.
    inline void PushMat16(lua_State* L, const float m[16])
    {
        lua_createtable(L, 16, 0);
        for (int i = 0; i < 16; ++i)
        {
            lua_pushnumber(L, static_cast<lua_Number>(m[i]));
            lua_rawseti(L, -2, i + 1);
        }
    }

    /// wxl.doodad.is_valid(ptr) -> bool: does the doodad_spawn pointer still read as a live placement.
    inline int L_isValid(lua_State* L)
    {
        void* d = CheckPtr(L, 1);
        Push(L, d != nullptr && gd::IsValid(d));
        return 1;
    }

    /// wxl.doodad.position(ptr) -> x, y, z (numbers) or nil. ptr is a doodad_spawn event lightuserdata.
    inline int L_position(lua_State* L)
    {
        void* d = CheckPtr(L, 1);
        if (!d || !gd::IsValid(d))
        {
            PushNil(L);
            return 1;
        }
        float p[3];
        gd::Position(d, p);
        Push(L, static_cast<double>(p[0]));
        Push(L, static_cast<double>(p[1]));
        Push(L, static_cast<double>(p[2]));
        return 3;
    }

    /// wxl.doodad.scale(ptr) -> number or nil (uniform scale). ptr is a doodad_spawn event lightuserdata.
    inline int L_scale(lua_State* L)
    {
        void* d = CheckPtr(L, 1);
        if (!d || !gd::IsValid(d))
        {
            PushNil(L);
            return 1;
        }
        Push(L, static_cast<double>(gd::Scale(d)));
        return 1;
    }

    /// wxl.doodad.flags(ptr) -> int or nil (placement flags, bit 0 = normal). ptr is a doodad_spawn
    /// event lightuserdata; the field has no game accessor so it is an SEH-guarded read off kFlags.
    inline int L_flags(lua_State* L)
    {
        void* d = CheckPtr(L, 1);
        uint32_t flags = 0;
        if (!d || !SehReadU32(d, off::kFlags, flags))
        {
            PushNil(L);
            return 1;
        }
        Push(L, static_cast<lua_Integer>(flags));
        return 1;
    }

    /// wxl.doodad.model_name(ptr) -> string or nil (bare .m2 file name; nil while still loading).
    /// ptr is a doodad_spawn event lightuserdata.
    inline int L_modelName(lua_State* L)
    {
        void* d = CheckPtr(L, 1);
        char buf[256];
        if (!d || !gd::ModelName(d, buf, sizeof(buf)))
        {
            PushNil(L);
            return 1;
        }
        Push(L, static_cast<const char*>(buf));
        return 1;
    }

    /// wxl.doodad.center(ptr) -> x, y, z (numbers) or nil (world bounding-sphere center, falls back to
    /// position). ptr is a doodad_spawn event lightuserdata.
    inline int L_center(lua_State* L)
    {
        void* d = CheckPtr(L, 1);
        if (!d || !gd::IsValid(d))
        {
            PushNil(L);
            return 1;
        }
        float c[3];
        gd::Center(d, c);
        Push(L, static_cast<double>(c[0]));
        Push(L, static_cast<double>(c[1]));
        Push(L, static_cast<double>(c[2]));
        return 3;
    }

    /// wxl.doodad.bbox(ptr) -> minX, minY, minZ, maxX, maxY, maxZ or nil (world-space box). ptr is a
    /// doodad_spawn event lightuserdata.
    inline int L_bbox(lua_State* L)
    {
        void* d = CheckPtr(L, 1);
        float mn[3], mx[3];
        if (!d || !gd::BBox(d, mn, mx))
        {
            PushNil(L);
            return 1;
        }
        Push(L, static_cast<double>(mn[0]));
        Push(L, static_cast<double>(mn[1]));
        Push(L, static_cast<double>(mn[2]));
        Push(L, static_cast<double>(mx[0]));
        Push(L, static_cast<double>(mx[1]));
        Push(L, static_cast<double>(mx[2]));
        return 6;
    }

    /// wxl.doodad.local_bounds(ptr) -> minX, minY, minZ, maxX, maxY, maxZ or nil (the model's real
    /// local AABB from the parsed MD20 header; nil while loading). ptr is a doodad_spawn event
    /// lightuserdata.
    inline int L_localBounds(lua_State* L)
    {
        void* d = CheckPtr(L, 1);
        float lo[3], hi[3];
        if (!d || !gd::LocalBounds(d, lo, hi))
        {
            PushNil(L);
            return 1;
        }
        Push(L, static_cast<double>(lo[0]));
        Push(L, static_cast<double>(lo[1]));
        Push(L, static_cast<double>(lo[2]));
        Push(L, static_cast<double>(hi[0]));
        Push(L, static_cast<double>(hi[1]));
        Push(L, static_cast<double>(hi[2]));
        return 6;
    }

    /// wxl.doodad.world_matrix(ptr) -> {16 numbers} or nil (the live world matrix the renderer reads,
    /// row-major, translation in m[13..15]). ptr is a doodad_spawn event lightuserdata.
    inline int L_worldMatrix(lua_State* L)
    {
        void* d = CheckPtr(L, 1);
        float m[16];
        if (!d || !gd::WorldMatrix(d, m))
        {
            PushNil(L);
            return 1;
        }
        PushMat16(L, m);
        return 1;
    }

    /**
     * @brief Adds the wxl.doodad subtable to the table on top of the stack. Stack-neutral.
     * @param L  the engine lua_State, with the target `wxl` table at index -1.
     */
    inline void Register(lua_State* L)
    {
        lua_newtable(L);                                                       // [wxl, doodad]
        lua_pushcfunction(L, &L_isValid);     lua_setfield(L, -2, "is_valid");
        lua_pushcfunction(L, &L_position);    lua_setfield(L, -2, "position");
        lua_pushcfunction(L, &L_scale);       lua_setfield(L, -2, "scale");
        lua_pushcfunction(L, &L_flags);       lua_setfield(L, -2, "flags");
        lua_pushcfunction(L, &L_modelName);   lua_setfield(L, -2, "model_name");
        lua_pushcfunction(L, &L_center);      lua_setfield(L, -2, "center");
        lua_pushcfunction(L, &L_bbox);        lua_setfield(L, -2, "bbox");
        lua_pushcfunction(L, &L_localBounds); lua_setfield(L, -2, "local_bounds");
        lua_pushcfunction(L, &L_worldMatrix); lua_setfield(L, -2, "world_matrix");
        lua_setfield(L, -2, "doodad");                                         // [wxl]
    }
}
