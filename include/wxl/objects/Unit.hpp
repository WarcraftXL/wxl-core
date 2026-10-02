// Unit: a creature or a player, with a body model and a reaction toward other units. Reads through
// the wxl::game::unit and wxl::game::world bindings.
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

#include "wxl/game/Unit.hpp"
#include "wxl/game/World.hpp"
#include "wxl/objects/Object.hpp"

namespace wxl
{
    /// A unit: a creature or a player, with a body model and a reaction toward other units.
    class Unit : public Object
    {
    public:
        Unit() = default;
        explicit Unit(void* raw) : Object(raw) {}

        /**
         * Resolves a GUID to a unit.
         *
         * @param uint64 guid
         * @return Unit unit : null when not resident or not a unit
         */
        static Unit FromGuid(uint64_t guid)
        { return Unit(game::world::ResolveObject(guid, game::world::kTypeMaskUnit)); }

        /**
         * The body model.
         *
         * @return void* model : null when none
         */
        void* Model() const { return game::unit::Model(raw_); }

        /**
         * The owned CharacterComponent, the state the equip and attach pipeline operates on.
         *
         * @return void* component : null for a non-humanoid unit or before it is built
         */
        void* CharacterComponent() const { return game::unit::CharacterComponent(raw_); }

        /**
         * The reaction of this unit toward another.
         *
         * @param Unit other
         * @return int32 reaction : 0..1 hostile, 2..3 neutral, 4 and above friendly
         */
        int Reaction(const Unit& other) const
        { return (raw_ && other.raw_) ? game::unit::Reaction(raw_, other.raw_) : 0; }

        /// True when the reaction toward other is hostile.
        bool IsHostileTo(const Unit& other) const { return Reaction(other) <= 1; }
        /// True when the reaction toward other is friendly.
        bool IsFriendlyTo(const Unit& other) const { return Reaction(other) >= 4; }
    };

    inline Unit Object::AsUnit() const { return IsUnit() ? Unit(raw_) : Unit(); }
}
