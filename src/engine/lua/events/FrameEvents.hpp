// Frame/world event context: the per-frame and world-transition events exposed to wxl.on.
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

/// One event context = one header declaring its events (name + bus event + arg marshaller) into the
/// shared bridge. Adding a context (RenderEvents, UnitEvents…) is a new header plus one Declare()
/// call in LuaEngine — no existing file grows. Events exposed here at the MVP:
///   "frame"       -> OnFrame       fn()        every Present; fires very often.
///   "update"      -> OnUpdate      fn(dt)      once per frame, dt = frame delta seconds.
///   "world_enter" -> OnWorldEnter  fn(mapId)   world/map finished loading, in-world.
///   "world_leave" -> OnWorldLeave  fn(mapId)   world/map is being torn down.
namespace wxl::lua::events::frame
{
    inline int PushNone(lua_State*, const void*)
    {
        return 0;
    }
    inline int PushUpdate(lua_State* L, const void* args)
    {
        lua_pushnumber(L, static_cast<const wxl::events::UpdateArgs*>(args)->dt);
        return 1;
    }
    inline int PushWorldEnter(lua_State* L, const void* args)
    {
        lua_pushnumber(L, static_cast<const wxl::events::WorldEnterArgs*>(args)->mapId);
        return 1;
    }
    inline int PushWorldLeave(lua_State* L, const void* args)
    {
        lua_pushnumber(L, static_cast<const wxl::events::WorldLeaveArgs*>(args)->mapId);
        return 1;
    }

    /// Registers this context's events with the bridge. Called once at install, before SubscribeBus.
    inline void Declare()
    {
        using wxl::events::Event;
        events::Declare("frame",       Event::OnFrame,      &PushNone);
        events::Declare("update",      Event::OnUpdate,     &PushUpdate);
        events::Declare("world_enter", Event::OnWorldEnter, &PushWorldEnter);
        events::Declare("world_leave", Event::OnWorldLeave, &PushWorldLeave);
    }
}
