// Object: a handle over any resident client object. One pointer, owns nothing, reads through the
// wxl::game::world bindings. AsUnit/AsPlayer are declared here and defined in Unit.hpp/Player.hpp,
// once those types are complete.
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
#include "wxl/game/World.hpp"

namespace wxl
{
    using game::world::Vec3;

    class Unit;
    class Player;
    class GameObject;

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
         * @param uint32 typeMask = kAnyType : the categories accepted, game::world::kTypeMask* bits
         * @return Object object : null when not resident or filtered out
         */
        static Object FromGuid(uint64_t guid, uint32_t typeMask = kAnyType)
        {
            return Object(game::world::ResolveObject(guid, typeMask));
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
        uint64_t Guid() const { return game::world::Guid(raw_); }

        /**
         * The categories the object belongs to.
         *
         * @return uint32 typeMask : game::world::kTypeMask* bits, 0 for a null handle
         */
        uint32_t TypeMask() const { return game::world::TypeMask(raw_); }

        /**
         * Tests the object against a category mask.
         *
         * @param uint32 typeMask : game::world::kTypeMask* bits
         * @return bool is
         */
        bool Is(uint32_t typeMask) const { return (TypeMask() & typeMask) != 0; }

        /// True for a unit or a player.
        bool IsUnit() const { return Is(game::world::kTypeMaskUnit); }
        /// True for a player.
        bool IsPlayer() const { return Is(game::world::kTypeMaskPlayer); }
        /// True for a game object.
        bool IsGameObject() const { return Is(game::world::kTypeMaskGameObject); }

        /**
         * The world position. A type without the slot reports the origin.
         *
         * @return Vec3 position
         */
        Vec3 Position() const
        {
            float p[3];
            game::world::Position(raw_, p);
            return { p[0], p[1], p[2] };
        }

        /**
         * The orientation, radians counter-clockwise from +X.
         *
         * @return float facing : 0 for a null handle
         */
        float Facing() const { return game::world::Facing(raw_); }

        /**
         * The anchor above the object where the client hangs its name.
         *
         * @return Vec3 position
         */
        Vec3 NamePosition() const
        {
            float p[3];
            game::world::NamePosition(raw_, p);
            return { p[0], p[1], p[2] };
        }

        /**
         * The same object as a Unit. Defined in wxl/objects/Unit.hpp.
         *
         * @return Unit unit : null when the object is not a unit
         */
        inline Unit AsUnit() const;

        /**
         * The same object as a Player. Defined in wxl/objects/Player.hpp.
         *
         * @return Player player : null when the object is not a player
         */
        inline Player AsPlayer() const;

        /**
         * The same object as a GameObject. Defined in wxl/objects/GameObject.hpp.
         *
         * @return GameObject gameObject : null when the object is not a game object
         */
        inline GameObject AsGameObject() const;

        bool operator==(const Object& o) const { return raw_ == o.raw_; }
        bool operator!=(const Object& o) const { return raw_ != o.raw_; }

    protected:
        void* raw_ = nullptr;
    };
}
