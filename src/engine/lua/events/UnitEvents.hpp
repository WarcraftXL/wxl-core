// Unit event context: exposes "target_changed" and proves an event can push an OBJECT, not a scalar.
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
#include "engine/lua/ObjectProxy.hpp"
#include "engine/lua/events/EventBridge.hpp"
#include "engine/events/Event.hpp"
#include "offsets/game/Unit.hpp"

/// One event context declaring the unit-facing events into the shared bridge. The marshaller here
/// pushes a Unit OBJECT (via ObjectProxy) rather than a raw GUID, so a handler receives the same
/// object API as wxl.target() — this is the vertical proof that events and methods share the proxy
/// layer. Events exposed:
///   "target_changed" -> OnTargetChanged  fn(unit)  the player's target was set (unit may be nil).
namespace wxl::lua::events::unit
{
    namespace off = wxl::offsets::game::unit;

    /// Reads the just-applied target GUID (the event carries only the script state) and pushes the
    /// Unit object, or nil when the target was cleared / the read faulted.
    inline int PushTargetChanged(lua_State* L, const void* /*args*/)
    {
        PushUnit(L, ReadGuidAt(off::kTargetGuid));
        return 1;
    }

    /// Registers this context's events with the bridge. Called once at install, before SubscribeBus.
    inline void Declare()
    {
        events::Declare("target_changed", wxl::events::Event::OnTargetChanged, &PushTargetChanged);
    }
}
