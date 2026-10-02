// Event bus: the readable hook surface modules subscribe to. The core owns the detours.
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

#include "wxl/Common.h"
#include "wxl/Events.hpp"

// An extension includes wxl/Events.hpp; this header is the core's bus over it.
#if defined(WXL_EXTENSION)
WXL_PRAGMA(message("engine/events/Event.hpp is internal to the core: include wxl/Events.hpp"))
#endif

/// Event bus: the core installs the detours and republishes them as named events. A subscriber is a
/// plain function pointer plus an opaque user, so Emit() walks a flat vector with no std::function
/// or vtable on the path.
namespace wxl::events
{
    /**
     * Subscribes a handler to an event for the process lifetime.
     *
     * @param Event e
     * @param Handler handler
     * @param void* user : handed back to the handler on every call
     */
    void Subscribe(Event e, Handler handler, void* user);

    /**
     * Publishes an event to its subscribers in subscription order, untyped: the ABI shim's path.
     *
     * @param Event e
     * @param void* args : the event's args struct
     */
    void EmitRaw(Event e, const void* args);

    /**
     * Publishes an event with its own args struct; another struct does not compile.
     *
     * @param Args<E> args
     */
    template <Event E>
    inline void Emit(const Args<E>& args) { EmitRaw(E, &args); }

    WXL_DEPRECATED("use Emit<Event::X>(args), which checks the args type")
    inline void Emit(Event e, const void* args) { EmitRaw(e, args); }

    /**
     * Reports whether an event has a subscriber. Subscription happens at load, before the render
     * detours run, so a hot path may read this unsynchronized.
     *
     * @param Event e
     * @return bool any
     */
    bool Any(Event e);
}
