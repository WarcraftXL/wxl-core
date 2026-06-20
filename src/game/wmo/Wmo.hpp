// WMO game bindings: typed inline wrappers over the client's map-object functions.
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
#include "offsets/game/WMO.hpp"

/**
 * @brief Typed inline wrappers over the client map-object functions, exposed as the WMO binding catalog.
 */
namespace wxl::game::wmo
{
    namespace off = wxl::offsets::game::wmo;

    /**
     * @brief Resolves a material's texture-name offsets. The native does not bounds-check materialIndex.
     * @param model          Map-object model.
     * @param materialIndex  Material index to resolve.
     */
    inline void ResolveMaterialTexture(void* model, int materialIndex)
    {
        Native<off::Wmo_ResolveMaterialTextureFn>(off::kResolveMaterialTexture)(model, nullptr, materialIndex);
    }

    /**
     * @brief Queries the resident state of a group, optionally forcing residency.
     * @param model       Map-object model.
     * @param groupIndex  Group index to query.
     * @param force       Nonzero forces the group resident.
     * @return The group's resident state.
     */
    inline unsigned int GroupResident(void* model, unsigned int groupIndex, unsigned int force)
    {
        return Native<off::Wmo_GroupResidentFn>(off::kGroupResidentAccessor)(model, nullptr, groupIndex, force);
    }

    /**
     * @brief Reads the root buffer pointer.
     * @param root  Map-object root.
     * @return The root buffer pointer, or null on a null root.
     */
    inline void* RootBuffer(void* root)
    {
        if (!root)
            return nullptr;
        return *reinterpret_cast<void**>(reinterpret_cast<char*>(root) + off::kOffRootBuffer);
    }

    /**
     * @brief Reads the group count (the group-array bound).
     * @param root  Map-object root.
     * @return The group count, or 0 on a null root.
     */
    inline uint32_t GroupCount(void* root)
    {
        if (!root)
            return 0;
        return *reinterpret_cast<uint32_t*>(reinterpret_cast<char*>(root) + off::kOffGroupCount);
    }

    /**
     * @brief Reads the group runtime object at an index.
     * @param root  Map-object root.
     * @param i     Group index.
     * @return The group object, or null when out of range or on a null root.
     */
    inline void* GroupAt(void* root, uint32_t i)
    {
        if (!root || i >= GroupCount(root))
            return nullptr;
        char* base = reinterpret_cast<char*>(root) + off::kOffGroupArray;
        return *reinterpret_cast<void**>(base + i * 4);
    }

    /** @brief Adds the WMO bindings to the enumerable catalog. */
    inline void RegisterCatalog()
    {
        Register({ "WMO::ResolveMaterialTexture", off::kResolveMaterialTexture,  "void(model, int materialIndex)" });
        Register({ "WMO::GroupResident",          off::kGroupResidentAccessor,   "uint(model, groupIndex, force)" });
    }
}
