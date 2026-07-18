// Event bridge: the common wxl::events -> Lua callback machinery behind wxl.on(name, fn).
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

#include "engine/events/Event.hpp"

struct lua_State;

/// The shared bridge behind wxl.on(eventName, fn). Event contexts (FrameEvents, future
/// RenderEvents…) Declare() their names once at install; SubscribeBus() then wires one dispatch
/// trampoline per declared event onto the core event bus. When an event fires, each registered Lua
/// function is called under lua_pcall; a handler that errors is logged (WLOG_WARN) and removed so a
/// broken extension cannot spam a per-frame event.
///
/// Ordering contract (matters because the bus range-iterates its subscribers and Subscribe must
/// never run inside an Emit): Declare() + SubscribeBus() happen ONCE at feature install, before any
/// frame. Bind()/Detach() run per VM state and touch only the Lua side (refs, the wxl table) — they
/// never call the bus, so a reload mid-frame cannot mutate the subscriber list during a walk.
namespace wxl::lua::events
{
    /// Pushes an event's argument(s) for its Lua handlers and returns how many were pushed. Lives in
    /// the owning context header so event-specific marshalling stays out of the generic bridge.
    using PushFn = int (*)(lua_State* L, const void* args);

    /**
     * @brief Declares an exposable event (install-time, from a context's Declare()).
     * @param name  the name extensions pass to wxl.on.
     * @param ev    the core bus event to bridge.
     * @param push  argument marshaller invoked before each handler call.
     */
    void Declare(const char* name, wxl::events::Event ev, PushFn push);

    /** @brief Subscribes one dispatch trampoline per declared event to the bus. Idempotent, once. */
    void SubscribeBus();

    /**
     * @brief Binds the bridge to a fresh state: clears stale callbacks and adds wxl.on to the table
     *        currently on top of the stack. Called by the engine at Start().
     * @param L  the engine lua_State, with the target `wxl` table at index -1.
     */
    void Bind(lua_State* L);

    /** @brief Detaches from the current state before it is closed (drops callbacks, nulls state). */
    void Detach();
}
