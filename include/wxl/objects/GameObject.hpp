// GameObject: a resident world object -- a door, a chest, a chair, a light. It carries the object
// header and the position slots Object reads, and the engine exposes nothing else about it, so the
// type is the Object surface under a name. Defines Object::AsGameObject.
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

namespace wxl
{
    /// A game object: a door, a chest, a chair, a light. Read through the Object surface.
    class GameObject : public Object
    {
    public:
        GameObject() = default;
        explicit GameObject(void* raw) : Object(raw) {}

        /**
         * Resolves a GUID to a game object.
         *
         * @param uint64 guid
         * @return GameObject gameObject : null when not resident or not a game object
         */
        static GameObject FromGuid(uint64_t guid)
        { return GameObject(game::world::ResolveObject(guid, game::world::kTypeMaskGameObject)); }
    };

    inline GameObject Object::AsGameObject() const
    { return IsGameObject() ? GameObject(raw_) : GameObject(); }
}
