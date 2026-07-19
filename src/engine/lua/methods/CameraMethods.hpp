// Camera method context: the wxl.camera subtable (position, matrices, world_to_screen).
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
#include "game/camera/Camera.hpp"

#include <windows.h>

/// The camera context, in the CoreMethods mould: one header whose Register() adds the wxl.camera
/// subtable to the `wxl` table on top of the stack. Every field reads a live engine global (the
/// view/projection matrices, the camera position) through the typed game/camera accessors, guarded
/// by the same SEH/POD pattern as ObjectProxy so an out-of-world read yields nil, never a fault.
/// The world_to_screen projection is factored into ObjectProxy::WorldToScreenPixels so unit
/// screen positions share one convention. Header-only and inline.
namespace wxl::lua::methods::camera
{
    namespace cam = wxl::game::camera;

    /// SEH-guarded POD copy of n floats from a live engine global (false on fault).
    inline bool ReadFloats(const float* src, float* dst, int n)
    {
        __try
        {
            for (int i = 0; i < n; ++i)
                dst[i] = src[i];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    /// Pushes a float[16] matrix as a 1-based Lua array of 16 numbers, or nil on fault.
    inline int PushMatrix(lua_State* L, const float* global)
    {
        float m[16];
        if (!ReadFloats(global, m, 16))
        {
            PushNil(L);
            return 1;
        }
        lua_createtable(L, 16, 0);
        for (int i = 0; i < 16; ++i)
        {
            lua_pushnumber(L, static_cast<lua_Number>(m[i]));
            lua_rawseti(L, -2, i + 1);
        }
        return 1;
    }

    /// wxl.camera.position() -> x, y, z (numbers) or nil when unavailable / faulted.
    inline int L_position(lua_State* L)
    {
        float p[3];
        __try
        {
            cam::Position(p);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            PushNil(L);
            return 1;
        }
        Push(L, static_cast<double>(p[0]));
        Push(L, static_cast<double>(p[1]));
        Push(L, static_cast<double>(p[2]));
        return 3;
    }

    /// wxl.camera.view() -> {16 numbers} (row-major world->view) or nil.
    inline int L_view(lua_State* L) { return PushMatrix(L, cam::View()); }

    /// wxl.camera.projection() -> {16 numbers} or nil.
    inline int L_projection(lua_State* L) { return PushMatrix(L, cam::Projection()); }

    /// wxl.camera.view_proj() -> {16 numbers} (View * Projection) or nil.
    inline int L_viewProj(lua_State* L) { return PushMatrix(L, cam::ViewProj()); }

    /// wxl.camera.world_to_screen(x, y, z) -> screenX, screenY, visible: ImGui pixel-space projection
    /// of a world point (top-left origin, live viewport). visible is false behind the camera or off
    /// screen; the pixel coords are still returned so callers can decide. nil when unprojectable.
    inline int L_worldToScreen(lua_State* L)
    {
        const float pos[3] = {
            static_cast<float>(CheckNumber(L, 1)),
            static_cast<float>(CheckNumber(L, 2)),
            static_cast<float>(CheckNumber(L, 3)),
        };
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

    /**
     * @brief Adds the wxl.camera subtable to the table on top of the stack. Stack-neutral.
     * @param L  the engine lua_State, with the target `wxl` table at index -1.
     */
    inline void Register(lua_State* L)
    {
        static const luaL_Reg fns[] = {
            { "position",        L_position },
            { "view",            L_view },
            { "projection",      L_projection },
            { "view_proj",       L_viewProj },
            { "world_to_screen", L_worldToScreen },
            { nullptr, nullptr },
        };
        RegisterModule(L, "camera", fns);
    }
}
