// Grass event context: the per-frame grass-wind tick exposed to wxl.on.
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
#include "engine/lua/events/EventBridge.hpp"
#include "engine/events/Event.hpp"

/// One event context declaring the grass-wind observation event into the shared bridge. The marshaller
/// pushes only the scalars the feature already computed into the args struct — no game memory is read, so
/// no SEH guard is needed. Events exposed here:
///   "grass_wind" -> OnGrassWind  fn(dirX, dirY, strength, phase)  the wind integrator advanced this frame.
namespace wxl::lua::events::grass
{
    /// OnGrassWind exposes the live damped wind vector, its target strength, and the accumulated sway
    /// phase (radians) — the same values wxl.wind.current()/strength() read, delivered per frame.
    inline int PushGrassWind(lua_State* L, const void* args)
    {
        const auto* a = static_cast<const wxl::events::GrassWindArgs*>(args);
        lua_pushnumber(L, a->dirX);
        lua_pushnumber(L, a->dirY);
        lua_pushnumber(L, a->strength);
        lua_pushnumber(L, a->phase);
        return 4;
    }

    /// Registers this context's event with the bridge. Called once at install, before SubscribeBus.
    inline void Declare()
    {
        using wxl::events::Event;
        events::Declare("grass_wind", Event::OnGrassWind, &PushGrassWind);
    }
}
