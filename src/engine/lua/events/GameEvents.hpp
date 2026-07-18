// Game event context: server object churn, sound, and cursor world-pick events exposed to wxl.on.
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

/// One event context declaring the game-world interaction events into the shared bridge. Every
/// marshaller here pushes only scalars the core already parsed into the args struct — none reads
/// game memory, so none needs an SEH guard (the packet/script-state pointers are left untouched
/// because their inner layout is not exposed as struct fields). Events exposed here:
///   "object_update" -> OnObjectUpdate   fn(opcode)      a server object create/field-delta batch was parsed.
///   "object_destroy"-> OnObjectDestroy  fn(opcode)      an object is about to despawn.
///   "sound_play"    -> OnSoundPlay       fn()            a UI/world sound is about to play.
///   "world_click"   -> OnWorldClick      fn(x,y,z,hitType) the cursor ray hit the world.
namespace wxl::lua::events::game
{
    /// OnObjectUpdate carries only the inbound message reader (cursor consumed) plus the opcode; no
    /// parsed GUID is exposed, so we push the opcode integer and leave the packet pointer untouched.
    inline int PushObjectUpdate(lua_State* L, const void* args)
    {
        lua_pushinteger(L, static_cast<const wxl::events::ObjectUpdateArgs*>(args)->opcode);
        return 1;
    }

    /// OnObjectDestroy likewise exposes only packet + opcode (the GUID/on-death flag live inside the
    /// packet reader, not as struct fields), so we push the opcode integer only.
    inline int PushObjectDestroy(lua_State* L, const void* args)
    {
        lua_pushinteger(L, static_cast<const wxl::events::ObjectDestroyArgs*>(args)->opcode);
        return 1;
    }

    /// OnSoundPlay carries only the opaque script state the call ran on (the sound id is on its
    /// stack, not a struct field), so there is nothing safe to marshal: the handler fires as fn().
    inline int PushSoundPlay(lua_State*, const void*)
    {
        return 0;
    }

    /// OnWorldClick exposes the resolved world hit point and the hit type as plain scalars; we push
    /// x,y,z and hitType (2 = M2/doodad, 3 = terrain/WMO). The object handle stays untouched.
    inline int PushWorldClick(lua_State* L, const void* args)
    {
        const auto* a = static_cast<const wxl::events::WorldClickArgs*>(args);
        lua_pushnumber(L, a->x);
        lua_pushnumber(L, a->y);
        lua_pushnumber(L, a->z);
        lua_pushinteger(L, a->hitType);
        return 4;
    }

    /// Registers this context's events with the bridge. Called once at install, before SubscribeBus.
    inline void Declare()
    {
        using wxl::events::Event;
        events::Declare("object_update",  Event::OnObjectUpdate,  &PushObjectUpdate);
        events::Declare("object_destroy", Event::OnObjectDestroy, &PushObjectDestroy);
        events::Declare("sound_play",     Event::OnSoundPlay,     &PushSoundPlay);
        events::Declare("world_click",    Event::OnWorldClick,    &PushWorldClick);
    }
}
