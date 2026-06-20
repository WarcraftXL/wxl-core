// Event bus: subscriber storage + dispatch.
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

#include "events/Event.hpp"

#include <vector>

namespace
{
    /// One subscriber: a handler function pointer and its opaque user pointer.
    struct Sub { wxl::events::Handler fn; void* user; };

    /**
     * @brief Returns the subscriber vector for an event, built on first use.
     * @param e  event whose bucket is requested.
     * @return reference to the event's subscriber vector.
     */
    std::vector<Sub>& Bucket(wxl::events::Event e)
    {
        static std::vector<Sub> buckets[static_cast<size_t>(wxl::events::Event::Count)];
        return buckets[static_cast<size_t>(e)];
    }
}

namespace wxl::events
{
    /**
     * @brief Appends a handler to an event's subscriber list.
     * @param e        event to subscribe to.
     * @param handler  function pointer invoked on Emit.
     * @param user     opaque pointer passed back to the handler.
     */
    void Subscribe(Event e, Handler handler, void* user)
    {
        if (e < Event::Count) Bucket(e).push_back({ handler, user });
    }

    /**
     * @brief Invokes every subscriber of an event in subscription order.
     * @param e     event to publish.
     * @param args  typed args struct for the event, passed by const pointer.
     */
    void Emit(Event e, const void* args)
    {
        if (e >= Event::Count) return;
        for (const Sub& s : Bucket(e)) s.fn(s.user, args);
    }
}
