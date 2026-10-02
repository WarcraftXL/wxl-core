// Object handles: Object, Unit and Player over the client's object manager. A handle is one pointer,
// owns nothing, and reads through the wxl::game::world and wxl::game::unit bindings.
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

#include "wxl/game/Pick.hpp"
#include "wxl/game/Unit.hpp"
#include "wxl/game/World.hpp"

namespace wxl::game
{
    using world::Vec3;

    class Unit;
    class Player;

    /// Any resident object: the header every type carries, the position slots every type implements.
    class Object
    {
    public:
        /// Every category, for a lookup that accepts any object.
        static constexpr uint32_t kAnyType = 0xFFFFFFFFu;

        Object() = default;

        /**
         * Wraps a raw object pointer.
         *
         * @param void* raw : the client object, may be null
         */
        explicit Object(void* raw) : raw_(raw) {}

        /**
         * Resolves a GUID to an object of an accepted category.
         *
         * @param uint64 guid
         * @param uint32 typeMask = kAnyType : the categories accepted, world::kTypeMask* bits
         * @return Object object : null when not resident or filtered out
         */
        static Object FromGuid(uint64_t guid, uint32_t typeMask = kAnyType)
        {
            return Object(world::ResolveObject(guid, typeMask));
        }

        /// True when the handle holds an object.
        explicit operator bool() const { return raw_ != nullptr; }

        /**
         * The raw client pointer, for the bindings that take one.
         *
         * @return void* raw
         */
        void* Raw() const { return raw_; }

        /**
         * The object's GUID.
         *
         * @return uint64 guid : 0 for a null handle
         */
        uint64_t Guid() const { return world::Guid(raw_); }

        /**
         * The categories the object belongs to.
         *
         * @return uint32 typeMask : world::kTypeMask* bits, 0 for a null handle
         */
        uint32_t TypeMask() const { return world::TypeMask(raw_); }

        /**
         * Tests the object against a category mask.
         *
         * @param uint32 typeMask : world::kTypeMask* bits
         * @return bool is
         */
        bool Is(uint32_t typeMask) const { return (TypeMask() & typeMask) != 0; }

        /// True for a unit or a player.
        bool IsUnit() const { return Is(world::kTypeMaskUnit); }
        /// True for a player.
        bool IsPlayer() const { return Is(world::kTypeMaskPlayer); }
        /// True for a game object.
        bool IsGameObject() const { return Is(world::kTypeMaskGameObject); }

        /**
         * The world position. A type without the slot reports the origin.
         *
         * @return Vec3 position
         */
        Vec3 Position() const
        {
            float p[3];
            world::Position(raw_, p);
            return { p[0], p[1], p[2] };
        }

        /**
         * The orientation, radians counter-clockwise from +X.
         *
         * @return float facing : 0 for a null handle
         */
        float Facing() const { return world::Facing(raw_); }

        /**
         * The anchor above the object where the client hangs its name.
         *
         * @return Vec3 position
         */
        Vec3 NamePosition() const
        {
            float p[3];
            world::NamePosition(raw_, p);
            return { p[0], p[1], p[2] };
        }

        /**
         * The same object as a Unit.
         *
         * @return Unit unit : null when the object is not a unit
         */
        inline Unit AsUnit() const;

        /**
         * The same object as a Player.
         *
         * @return Player player : null when the object is not a player
         */
        inline Player AsPlayer() const;

        bool operator==(const Object& o) const { return raw_ == o.raw_; }
        bool operator!=(const Object& o) const { return raw_ != o.raw_; }

    protected:
        void* raw_ = nullptr;
    };

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
        static Unit FromGuid(uint64_t guid) { return Unit(world::ResolveObject(guid, world::kTypeMaskUnit)); }

        /**
         * The body model.
         *
         * @return void* model : null when none
         */
        void* Model() const { return unit::Model(raw_); }

        /**
         * The owned CharacterComponent, the state the equip and attach pipeline operates on.
         *
         * @return void* component : null for a non-humanoid unit or before it is built
         */
        void* CharacterComponent() const { return unit::CharacterComponent(raw_); }

        /**
         * The reaction of this unit toward another.
         *
         * @param Unit other
         * @return int32 reaction : 0..1 hostile, 2..3 neutral, 4 and above friendly
         */
        int Reaction(const Unit& other) const { return (raw_ && other.raw_) ? unit::Reaction(raw_, other.raw_) : 0; }

        /// True when the reaction toward other is hostile.
        bool IsHostileTo(const Unit& other) const { return Reaction(other) <= 1; }
        /// True when the reaction toward other is friendly.
        bool IsFriendlyTo(const Unit& other) const { return Reaction(other) >= 4; }
    };

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
        static Player FromGuid(uint64_t guid) { return Player(world::ResolveObject(guid, world::kTypeMaskPlayer)); }

        /**
         * The active player.
         *
         * @return Player player : null outside the world
         */
        static Player Active() { return FromGuid(world::ActivePlayerGuid()); }

        /**
         * The current target.
         *
         * @return Object target : null when nothing is targeted or the target is not resident
         */
        static Object Target() { return Object::FromGuid(world::TargetGuid()); }

        /**
         * The object under the cursor.
         *
         * @return Object mouseover : null when none
         */
        static Object Mouseover() { return Object::FromGuid(world::MouseoverGuid()); }
    };

    inline Unit   Object::AsUnit() const   { return IsUnit()   ? Unit(raw_)   : Unit(); }
    inline Player Object::AsPlayer() const { return IsPlayer() ? Player(raw_) : Player(); }
}
