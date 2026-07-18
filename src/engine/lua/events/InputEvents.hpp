// Input event context: the swallowable window-message event exposed to wxl.on.
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

/// One event context declaring the input event into the shared bridge:
///   "input" -> OnInput   fn(message, wparam, lparam) -> boolean   a window input message arrived.
///
/// SWALLOW CONTRACT: a handler that RETURNS a truthy value marks the message handled, so the core
/// swallows it and the game does not also react (e.g. a hotkey the extension owns). This is wired via
/// the bridge's ReturnFn path: "input" is Declare()d with ConsumeInput, which sets the args' *handled
/// out-param when a handler returns true. Returns are sticky/OR across multiple handlers — once any
/// handler swallows the message, later handlers cannot un-swallow it. A handler that returns nil/false
/// only observes. message/wparam/lparam are pushed as numbers ONLY; wparam/lparam are never
/// dereferenced as pointers on this layer. Note ImGui already consumes input when it wants capture;
/// OnInput fires regardless per the core's contract, so a handler should test message itself.
namespace wxl::lua::events::input
{
    /// OnInput exposes the raw message id and the two message parameters as numbers. wparam/lparam are
    /// marshalled as opaque numeric values, never dereferenced. The *handled out-param is written only
    /// by ConsumeInput below, from the handler's return.
    inline int PushInput(lua_State* L, const void* args)
    {
        const auto* a = static_cast<const wxl::events::InputArgs*>(args);
        lua_pushinteger(L, static_cast<lua_Integer>(a->message));
        lua_pushnumber(L, static_cast<lua_Number>(a->wparam));
        lua_pushnumber(L, static_cast<lua_Number>(a->lparam));
        return 3;
    }

    /// Consumes the handler's return: a truthy result sets *handled so the core swallows the message.
    /// A false/nil return leaves *handled as it was, so observe-only handlers never swallow.
    inline void ConsumeInput(const void* args, bool ret)
    {
        if (!ret)
            return;
        const auto* a = static_cast<const wxl::events::InputArgs*>(args);
        if (a->handled)
            *a->handled = true;
    }

    /// Registers this context's events with the bridge. Called once at install, before SubscribeBus.
    inline void Declare()
    {
        events::Declare("input", wxl::events::Event::OnInput, &PushInput, &ConsumeInput);
    }
}
