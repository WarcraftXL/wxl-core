// Player: the player character and the selection globals around it (active player, target,
// mouseover). Reads through the wxl::game::world bindings.
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

#include <cstdint>

#include "wxl/game/World.hpp"
#include "wxl/objects/Object.hpp"
#include "wxl/objects/Unit.hpp"

namespace wxl
{
    /// The player character and the selection globals around it.
    class Player : public Unit
    {
    public:
        Player() = default;
        explicit Player(void* raw) : Unit(raw) {}

        /**
         * Resolves a GUID to a player.
         *
         * @param uint64 guid
         * @return Player player : null when not resident or not a player
         */
        static Player FromGuid(uint64_t guid)
        { return Player(game::world::ResolveObject(guid, game::world::kTypeMaskPlayer)); }

        /**
         * The active player.
         *
         * @return Player player : null outside the world
         */
        static Player Active() { return FromGuid(game::world::ActivePlayerGuid()); }

        /**
         * The current target.
         *
         * @return Object target : null when nothing is targeted or the target is not resident
         */
        static Object Target() { return Object::FromGuid(game::world::TargetGuid()); }

        /**
         * The object under the cursor.
         *
         * @return Object mouseover : null when none
         */
        static Object Mouseover() { return Object::FromGuid(game::world::MouseoverGuid()); }
    };

    inline Player Object::AsPlayer() const { return IsPlayer() ? Player(raw_) : Player(); }
}
