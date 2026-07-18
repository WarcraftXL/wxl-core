// UI event context: the per-frame 'draw' slot exposed to wxl.on, where wxl.ui.* is legal.
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

/// One event context declaring the UI draw slot into the shared bridge:
///   "draw" -> OnUiDraw   fn()   once per frame, between ImGui NewFrame and Render.
///
/// CONTRACT: wxl.ui.* is valid ONLY inside a handler of this event. The ImGui host (ImGuiHost.cpp) opens
/// the frame at Present, emits OnUiDraw, then closes/renders it at the next EndScene; a wxl.ui.* call from
/// any other event or from module scope raises a Lua error. Args are empty (the ImGui frame is implicit).
namespace wxl::lua::events::ui
{
    inline int PushNone(lua_State*, const void*)
    {
        return 0;
    }

    /// Registers this context's events with the bridge. Called once at install, before SubscribeBus.
    inline void Declare()
    {
        events::Declare("draw", wxl::events::Event::OnUiDraw, &PushNone);
    }
}
