// Wind method context: the wxl.wind subtable (grass wind + player-parting tuning).
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
#include "features/grasswind/GrassWind.hpp"

/// The wind context: Register() adds the wxl.wind subtable to the `wxl` table on the stack. Every field
/// of the grass motion settings (features::grasswind::WindSettings + PhysicsSettings) is exposed so a Lua
/// extension drives the sway and the player-parting entirely. No game memory is touched (the settings are
/// plain WXL structs on the game thread), so no SEH guard is needed. The event side is
/// wxl.on("grass_wind", ...) (GrassEvents). Header-only and inline.
namespace wxl::lua::methods::wind
{
    namespace gw = wxl::features::grasswind;

    inline int L_installed(lua_State* L) { Push(L, gw::Installed()); return 1; }

    // --- wind ---
    inline int L_enabled(lua_State* L) { Push(L, gw::Wind().enabled); return 1; }
    inline int L_setEnabled(lua_State* L) { gw::Wind().enabled = CheckBool(L, 1); return 0; }

    inline int L_direction(lua_State* L) { Push(L, static_cast<double>(gw::Wind().directionDeg)); return 1; }
    inline int L_setDirection(lua_State* L) { gw::Wind().directionDeg = static_cast<float>(CheckNumber(L, 1)); return 0; }

    inline int L_speed(lua_State* L) { Push(L, static_cast<double>(gw::Wind().speed)); return 1; }
    inline int L_setSpeed(lua_State* L) { gw::Wind().speed = static_cast<float>(CheckNumber(L, 1)); return 0; }

    inline int L_amplitude(lua_State* L) { Push(L, static_cast<double>(gw::Wind().amplitude)); return 1; }
    inline int L_setAmplitude(lua_State* L)
    {
        double v = CheckNumber(L, 1); if (v < 0.0) v = 0.0;
        gw::Wind().amplitude = static_cast<float>(v); return 0;
    }

    inline int L_wavelength(lua_State* L) { Push(L, static_cast<double>(gw::Wind().wavelength)); return 1; }
    inline int L_setWavelength(lua_State* L) { gw::Wind().wavelength = static_cast<float>(CheckNumber(L, 1)); return 0; }

    inline int L_lean(lua_State* L) { Push(L, static_cast<double>(gw::Wind().lean)); return 1; }
    inline int L_setLean(lua_State* L) { gw::Wind().lean = static_cast<float>(CheckNumber(L, 1)); return 0; }

    inline int L_variance(lua_State* L) { Push(L, static_cast<double>(gw::Wind().variance)); return 1; }
    inline int L_setVariance(lua_State* L)
    {
        double v = CheckNumber(L, 1); if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0;
        gw::Wind().variance = static_cast<float>(v); return 0;
    }

    inline int L_anchor(lua_State* L) { Push(L, static_cast<double>(gw::Wind().anchor)); return 1; }
    inline int L_setAnchor(lua_State* L)
    {
        double v = CheckNumber(L, 1); if (v < 0.0) v = 0.0; if (v > 0.9) v = 0.9;
        gw::Wind().anchor = static_cast<float>(v); return 0;
    }

    inline int L_distanceFade(lua_State* L) { Push(L, static_cast<double>(gw::Wind().distanceFade)); return 1; }
    inline int L_setDistanceFade(lua_State* L)
    {
        double v = CheckNumber(L, 1); if (v < 0.0) v = 0.0;
        gw::Wind().distanceFade = static_cast<float>(v); return 0;
    }

    // --- cross swell (secondary wave) ---
    inline int L_crossAmplitude(lua_State* L) { Push(L, static_cast<double>(gw::Wind().crossAmplitude)); return 1; }
    inline int L_setCrossAmplitude(lua_State* L)
    {
        double v = CheckNumber(L, 1); if (v < 0.0) v = 0.0;
        gw::Wind().crossAmplitude = static_cast<float>(v); return 0;
    }
    inline int L_crossAngle(lua_State* L) { Push(L, static_cast<double>(gw::Wind().crossAngleDeg)); return 1; }
    inline int L_setCrossAngle(lua_State* L) { gw::Wind().crossAngleDeg = static_cast<float>(CheckNumber(L, 1)); return 0; }

    // --- repulsion (blades part around the player) ---
    inline int L_repelEnabled(lua_State* L) { Push(L, gw::Physics().enabled); return 1; }
    inline int L_setRepelEnabled(lua_State* L) { gw::Physics().enabled = CheckBool(L, 1); return 0; }
    inline int L_repelRadius(lua_State* L) { Push(L, static_cast<double>(gw::Physics().radius)); return 1; }
    inline int L_setRepelRadius(lua_State* L)
    {
        double v = CheckNumber(L, 1); if (v < 0.0) v = 0.0;
        gw::Physics().radius = static_cast<float>(v); return 0;
    }
    inline int L_repelForce(lua_State* L) { Push(L, static_cast<double>(gw::Physics().forceCenter)); return 1; }
    inline int L_setRepelForce(lua_State* L) { gw::Physics().forceCenter = static_cast<float>(CheckNumber(L, 1)); return 0; }

    /**
     * @brief Adds the wxl.wind subtable to the table on top of the stack. Stack-neutral.
     * @param L  the engine lua_State, with the target `wxl` table at index -1.
     */
    inline void Register(lua_State* L)
    {
        static const luaL_Reg fns[] = {
            { "installed",           L_installed },
            { "enabled",             L_enabled },
            { "set_enabled",         L_setEnabled },
            { "direction",           L_direction },
            { "set_direction",       L_setDirection },
            { "speed",               L_speed },
            { "set_speed",           L_setSpeed },
            { "amplitude",           L_amplitude },
            { "set_amplitude",       L_setAmplitude },
            { "wavelength",          L_wavelength },
            { "set_wavelength",      L_setWavelength },
            { "lean",                L_lean },
            { "set_lean",            L_setLean },
            { "variance",            L_variance },
            { "set_variance",        L_setVariance },
            { "anchor",              L_anchor },
            { "set_anchor",          L_setAnchor },
            { "distance_fade",       L_distanceFade },
            { "set_distance_fade",   L_setDistanceFade },
            { "cross_amplitude",     L_crossAmplitude },
            { "set_cross_amplitude", L_setCrossAmplitude },
            { "cross_angle",         L_crossAngle },
            { "set_cross_angle",     L_setCrossAngle },
            { "repel_enabled",       L_repelEnabled },
            { "set_repel_enabled",   L_setRepelEnabled },
            { "repel_radius",        L_repelRadius },
            { "set_repel_radius",    L_setRepelRadius },
            { "repel_force",         L_repelForce },
            { "set_repel_force",     L_setRepelForce },
            { nullptr, nullptr },
        };
        RegisterModule(L, "wind", fns);
    }
}
