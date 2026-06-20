// EventScript: the OOP base a runtime script subclasses to handle events as member functions.
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

#include "events/Event.hpp"

/// A runtime script subclasses EventScript and, in its ctor, binds member functions to events with
/// on<&Self::method>(Event::X). The bind is a non-capturing trampoline (a plain function pointer), so
/// the event bus stays POD-dispatch with no std::function on the path. Each handler is a typed member,
/// e.g. void onFrame(const events::FrameArgs& a).
namespace wxl::events
{
    namespace detail
    {
        /// Deduces the owning class and the args type from a void (C::*)(const A&) member pointer.
        template <class C, class A> struct MemArg { using Class = C; using Arg = A; };
        template <class C, class A> MemArg<C, A> MemArgOf(void (C::*)(const A&));
    }

    /// Base class a runtime script subclasses to bind member functions as event handlers.
    class EventScript
    {
    protected:
        EventScript() = default;
        virtual ~EventScript() = default;

        /**
         * @brief Binds a member function to an event; the class and args type are deduced from Method.
         * @param e  event to bind the member function to.
         */
        template <auto Method>
        void on(Event e)
        {
            using Traits = decltype(detail::MemArgOf(Method));
            using Self   = typename Traits::Class;
            using Arg    = typename Traits::Arg;
            Subscribe(e, [](void* user, const void* args) {
                Self* self = static_cast<Self*>(static_cast<EventScript*>(user));
                (self->*Method)(*static_cast<const Arg*>(args));
            }, this);
        }
    };
}
