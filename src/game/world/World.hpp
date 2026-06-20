// world bindings: selection GUIDs and GUID-to-object resolution.
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
 * @brief Typed accessors for the current selection GUIDs and GUID-to-object resolution.
 */
namespace wxl::game::world
{
    namespace off = wxl::offsets::game::unit;

    /**
     * @brief Reads the GUID of the unit under the cursor.
     * @return The mouseover GUID.
     */
    inline unsigned long long MouseoverGuid()
    { return *reinterpret_cast<unsigned long long*>(off::kMouseoverGuid); }

    /**
     * @brief Reads the GUID of the current target.
     * @return The target GUID.
     */
    inline unsigned long long TargetGuid()
    { return *reinterpret_cast<unsigned long long*>(off::kTargetGuid); }

    /**
     * @brief Reads the GUID of the active player.
     * @return The active player GUID.
     */
    inline unsigned long long ActivePlayerGuid()
    { return Native<off::ActivePlayerGuidFn>(off::kActivePlayerGuid)(); }

    /**
     * @brief Resolves a GUID to an object filtered by type mask.
     * @param guid      Object GUID.
     * @param typeMask  Accepted object type mask.
     * @return The object, or null when not found or filtered out.
     */
    inline void* GetObject(unsigned long long guid, unsigned typeMask)
    { return Native<off::GetObjectFn>(off::kGetObjectByGuid)(guid, typeMask, "wxl", 0); }

    constexpr unsigned kTypeMaskUnit   = off::kTypeMaskUnit;
    constexpr unsigned kTypeMaskPlayer = off::kTypeMaskPlayer;
}
