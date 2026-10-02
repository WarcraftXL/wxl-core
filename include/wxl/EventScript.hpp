// Member functions as event handlers, for an extension.
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

#include "wxl/PluginApi.h"

#include "wxl/Events.hpp"

/// An extension subclasses EventScript and, in its constructor, binds member functions to events
/// with on<&Self::method>(Event::X). The bind is a non-capturing trampoline, so the event bus keeps
/// dispatching through a plain function pointer. Each handler is a typed member, e.g.
/// void OnFrame(const events::FrameArgs& a).
namespace wxl::ext
{
    namespace detail
    {
        /// Deduces the owning class and the args type from a void (C::*)(const A&) member pointer.
        template <class C, class A> struct MemArg { using Class = C; using Arg = A; };
        template <class C, class A> MemArg<C, A> MemArgOf(void (C::*)(const A&));
    }

    class EventScript
    {
    public:
        /**
         * @brief Records the table the bindings go through.
         *
         * Call once from WXL_Load, before constructing any subclass: an extension has no table until
         * it is handed one, so a subclass built at static-initialisation time would bind to nothing.
         * @param api  the table WXL_Load received.
         */
        static void Bind(const WXL_Api* api) { s_api = api; }

    protected:
        EventScript() = default;
        virtual ~EventScript() = default;

        /**
         * @brief Binds a member function to an event; the class and args type are deduced from Method.
         * @param e  event to bind the member function to.
         */
        template <auto Method>
        void on(events::Event e)
        {
            using Traits = decltype(detail::MemArgOf(Method));
            using Self   = typename Traits::Class;
            using Arg    = typename Traits::Arg;

            if (!s_api) return;
            s_api->Subscribe(uint32_t(e), [](void* user, const void* args) {
                Self* self = static_cast<Self*>(static_cast<EventScript*>(user));
                (self->*Method)(*static_cast<const Arg*>(args));
            }, this);
        }

    private:
        static inline const WXL_Api* s_api = nullptr;
    };
}
