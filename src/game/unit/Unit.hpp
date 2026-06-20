// unit bindings: model access and reaction.
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

#include "game/Binding.hpp"
#include "offsets/game/Unit.hpp"

/**
 * @brief Typed accessors for unit model fields and the reaction call.
 */
namespace wxl::game::unit
{
    namespace off = wxl::offsets::game::unit;

    /**
     * @brief Reads the unit's body model.
     * @param unit  Unit object.
     * @return The body model, or null.
     */
    inline void* Model(void* unit)
    { return unit ? *reinterpret_cast<void**>(reinterpret_cast<char*>(unit) + off::kUnitModelField) : nullptr; }

    /**
     * @brief Reads a model's parent in the attachment chain.
     * @param model  Model object.
     * @return The parent model, or null at the root.
     */
    inline void* ModelParent(void* model)
    { return model ? *reinterpret_cast<void**>(reinterpret_cast<char*>(model) + off::kModelParentField) : nullptr; }

    /**
     * @brief Reads the reaction of self toward other.
     * @param self   Observing unit.
     * @param other  Observed unit.
     * @return Reaction level: 0..1 hostile, 2..3 neutral, 4+ friendly.
     */
    inline int Reaction(void* self, void* other)
    { return Native<off::ReactionFn>(off::kUnitReaction)(self, nullptr, other); }
}
