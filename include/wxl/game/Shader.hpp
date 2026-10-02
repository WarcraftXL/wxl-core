// shader bindings: the programmable-path selection state, as a typed facade over the pinned offsets.
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

#include "wxl/game/Binding.hpp"
#include "wxl/offsets/engine/Shader.hpp"

/**
 * @brief Read access to the engine's programmable-shader selection state.
 *
 * Anything outside the core that wants to know WHICH shader permutation is about to be bound has two
 * bad options and this third one: include the offsets tree directly (which is the SDK boundary an
 * extension exists on the far side of), or copy the addresses into itself (which forks the single
 * place a client rebase is applied). This facade is the third option -- the addresses stay pinned in
 * one file, and callers reach them as `shader::off::...` alongside the readers below.
 *
 * Every reader here is a raw load and nothing more. What a value MEANS -- which permutation is worth
 * naming, which collection is "the character shader" -- is policy that changes with whatever is being
 * built on top, so it belongs to the caller and deliberately has no representation here.
 */
namespace wxl::game::shader
{
    namespace off = wxl::offsets::engine::shader;

    /// Entries in either effect table. The last slot is the two-layer composite effect.
    constexpr unsigned kExteriorEffectCount = 7;
    /// The alternate table carries the same seven slots at the same positions.
    constexpr unsigned kAltEffectCount = kExteriorEffectCount;

    /**
     * @brief Reads the effect collection the last activate selected.
     * @return the collection pointer, or null before the first activate of the frame.
     */
    inline const void* ActiveCollection()
    { return Read<const void*>(off::kActiveCollection); }

    /**
     * @brief Reads the shadow tier feeding the permutation choice.
     * @return the tier, clamped by the engine to 0..2.
     */
    inline int ShadowTier() { return Read<int>(off::kShadowTier); }

    /**
     * @brief Reads the pixel-side shadow group feeding the permutation choice.
     * @return the group index.
     */
    inline int ShadowGroup() { return Read<int>(off::kShadowGroup); }

    /**
     * @brief Reads the light/fog bit feeding the permutation choice.
     * @return 0 or 1.
     */
    inline int LightBit() { return Read<int>(off::kLightBit); }

    /**
     * @brief Reads the permutation sub-index feeding the permutation choice.
     * @return the sub-index, 0..14.
     */
    inline int SubIndex() { return Read<int>(off::kSubIndex); }

    /**
     * @brief Reads the master flag gating the whole programmable path.
     * @return true when effects are bound from shader collections rather than fixed function.
     */
    inline bool ProgrammablePathActive()
    { return Read<uint32_t>(off::kProgrammablePathFlag) != 0; }

    /**
     * @brief Reads one slot of the exterior effect table.
     * @param index  the slot, 0 .. kExteriorEffectCount - 1.
     * @return the registered effect-collection pointer, or null when the slot is out of range or the
     *         effect subsystem has not registered it yet.
     */
    inline const void* ExteriorEffect(unsigned index)
    {
        if (index >= kExteriorEffectCount) return nullptr;
        return reinterpret_cast<const void* const*>(off::kExteriorEffectTable)[index];
    }

    /**
     * @brief Reads one slot of the alternate effect table.
     *
     * A second table exists because the delegated renderer a map object with the modern flag set is
     * routed to binds from here instead. So a batch of modern content leaves an active collection
     * that appears nowhere in the exterior table, and anything matching a collection against that
     * table alone reports modern draws as unrecognised -- which is precisely the content most worth
     * recognising. Any caller resolving a collection has to try both.
     * @param index  the slot, 0 .. kAltEffectCount - 1.
     * @return the registered effect-collection pointer, or null when the slot is out of range or the
     *         effect subsystem has not registered it yet.
     */
    inline const void* AltEffect(unsigned index)
    {
        if (index >= kAltEffectCount) return nullptr;
        return reinterpret_cast<const void* const*>(off::kAltEffectTable)[index];
    }
}
