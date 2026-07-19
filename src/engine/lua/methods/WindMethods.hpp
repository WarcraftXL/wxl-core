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
        lua_newtable(L);                                                                      // [wxl, wind]
        lua_pushcfunction(L, &L_installed);        lua_setfield(L, -2, "installed");
        lua_pushcfunction(L, &L_enabled);          lua_setfield(L, -2, "enabled");
        lua_pushcfunction(L, &L_setEnabled);       lua_setfield(L, -2, "set_enabled");
        lua_pushcfunction(L, &L_direction);        lua_setfield(L, -2, "direction");
        lua_pushcfunction(L, &L_setDirection);     lua_setfield(L, -2, "set_direction");
        lua_pushcfunction(L, &L_speed);            lua_setfield(L, -2, "speed");
        lua_pushcfunction(L, &L_setSpeed);         lua_setfield(L, -2, "set_speed");
        lua_pushcfunction(L, &L_amplitude);        lua_setfield(L, -2, "amplitude");
        lua_pushcfunction(L, &L_setAmplitude);     lua_setfield(L, -2, "set_amplitude");
        lua_pushcfunction(L, &L_wavelength);       lua_setfield(L, -2, "wavelength");
        lua_pushcfunction(L, &L_setWavelength);    lua_setfield(L, -2, "set_wavelength");
        lua_pushcfunction(L, &L_lean);             lua_setfield(L, -2, "lean");
        lua_pushcfunction(L, &L_setLean);          lua_setfield(L, -2, "set_lean");
        lua_pushcfunction(L, &L_variance);         lua_setfield(L, -2, "variance");
        lua_pushcfunction(L, &L_setVariance);      lua_setfield(L, -2, "set_variance");
        lua_pushcfunction(L, &L_anchor);           lua_setfield(L, -2, "anchor");
        lua_pushcfunction(L, &L_setAnchor);        lua_setfield(L, -2, "set_anchor");
        lua_pushcfunction(L, &L_distanceFade);     lua_setfield(L, -2, "distance_fade");
        lua_pushcfunction(L, &L_setDistanceFade);  lua_setfield(L, -2, "set_distance_fade");
        lua_pushcfunction(L, &L_crossAmplitude);   lua_setfield(L, -2, "cross_amplitude");
        lua_pushcfunction(L, &L_setCrossAmplitude);lua_setfield(L, -2, "set_cross_amplitude");
        lua_pushcfunction(L, &L_crossAngle);       lua_setfield(L, -2, "cross_angle");
        lua_pushcfunction(L, &L_setCrossAngle);    lua_setfield(L, -2, "set_cross_angle");
        lua_pushcfunction(L, &L_repelEnabled);     lua_setfield(L, -2, "repel_enabled");
        lua_pushcfunction(L, &L_setRepelEnabled);  lua_setfield(L, -2, "set_repel_enabled");
        lua_pushcfunction(L, &L_repelRadius);      lua_setfield(L, -2, "repel_radius");
        lua_pushcfunction(L, &L_setRepelRadius);   lua_setfield(L, -2, "set_repel_radius");
        lua_pushcfunction(L, &L_repelForce);       lua_setfield(L, -2, "repel_force");
        lua_pushcfunction(L, &L_setRepelForce);    lua_setfield(L, -2, "set_repel_force");
        lua_setfield(L, -2, "wind");                                                          // [wxl]
    }
}
