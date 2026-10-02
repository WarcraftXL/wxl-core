// WMO game bindings: typed wrappers over the client's map-object functions.
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
#include <cstring>

#include "wxl/game/Binding.hpp"
#include "wxl/offsets/game/WMO.hpp"

/**
 * @brief Typed wrappers over the client map-object functions, exposed as the WMO binding catalog.
 */
namespace wxl::game::wmo
{
    namespace off = wxl::offsets::game::wmo;

    /**
     * @brief Thin typed facade over one map-object root: its raw chunk buffer and group array.
     *
     * Each method is a zero-overhead inline read/call; method names mirror the Get/Set convention
     * `wxl::game::gx::Device9` and `wxl::game::m2::M2Model` already established for this SDK layer.
     */
    class WmoRoot
    {
    public:
        /**
         * @brief Wraps a raw map-object root pointer.
         * @param root  the raw root pointer, possibly null.
         */
        explicit WmoRoot(void* root) : root_(root) {}

        /** @brief Returns the wrapped raw root pointer. */
        void* raw() const { return root_; }

        /** @brief Returns true if a root is wrapped. */
        explicit operator bool() const { return root_ != nullptr; }

        /** @brief Reads the root buffer pointer, or null on a null root. */
        void* GetBuffer() const
        {
            return root_ ? reinterpret_cast<void*>(static_cast<off::Root*>(root_)->rootBuffer) : nullptr;
        }

        /** @brief Reads the root buffer byte size (the bound the native chunk walk reads to), or 0 on a null root. */
        uint32_t GetSize() const
        {
            return root_ ? static_cast<off::Root*>(root_)->rootSize : 0;
        }

        /**
         * @brief Sets the root buffer byte size after reshaping the buffer in place.
         * @param size  new byte size the native chunk walk should read to.
         */
        void SetSize(uint32_t size)
        {
            if (root_) static_cast<off::Root*>(root_)->rootSize = size;
        }

        /** @brief Reads the group count (the group-array bound), or 0 on a null root. */
        uint32_t GetGroupCount() const
        {
            return root_ ? static_cast<off::Root*>(root_)->groupCount : 0;
        }

        /**
         * @brief Reads the group runtime object at an index.
         * @param i  group index.
         * @return the group object, or null when out of range or on a null root.
         */
        void* GetGroupAt(uint32_t i) const
        {
            if (!root_ || i >= GetGroupCount())
                return nullptr;
            return static_cast<off::Root*>(root_)->groupArray[i];
        }

        /**
         * @brief Queries the resident state of a group, optionally forcing residency.
         * @param groupIndex  group index to query.
         * @param force       nonzero forces the group resident.
         * @return the group's resident state.
         */
        unsigned GetGroupResident(unsigned groupIndex, unsigned force) const
        {
            return Native<off::Wmo_GroupResidentFn>(off::kGroupResidentAccessor)(root_, nullptr, groupIndex, force);
        }

        /**
         * @brief Resolves a material's texture-name offsets. The native does not bounds-check materialIndex.
         * @param materialIndex  material index to resolve.
         */
        void ResolveMaterialTexture(int materialIndex)
        {
            Native<off::Wmo_ResolveMaterialTextureFn>(off::kResolveMaterialTexture)(root_, nullptr, materialIndex);
        }

    private:
        void* root_;
    };

    /**
     * @brief Thin typed facade over one map-object group: its raw sub-chunk buffer.
     */
    class WmoGroup
    {
    public:
        /**
         * @brief Wraps a raw map-object group pointer.
         * @param group  the raw group pointer, possibly null.
         */
        explicit WmoGroup(void* group) : group_(group) {}

        /** @brief Returns the wrapped raw group pointer. */
        void* raw() const { return group_; }

        /** @brief Returns true if a group is wrapped. */
        explicit operator bool() const { return group_ != nullptr; }

        /** @brief Reads the group buffer pointer, or null on a null group. */
        void* GetBuffer() const
        {
            return group_ ? reinterpret_cast<void*>(static_cast<off::Group*>(group_)->groupBuffer) : nullptr;
        }

        /** @brief Reads the group buffer byte size (the bound the native sub-chunk walk reads to), or 0 on a null group. */
        uint32_t GetSize() const
        {
            return group_ ? static_cast<off::Group*>(group_)->groupSize : 0;
        }

        /**
         * @brief Sets the group buffer byte size after reshaping the buffer in place.
         * @param size  new byte size the native sub-chunk walk should read to.
         */
        void SetSize(uint32_t size)
        {
            if (group_) static_cast<off::Group*>(group_)->groupSize = size;
        }

    private:
        void* group_;
    };

    /**
     * @brief Returns the inline file path of the map-object the camera is currently inside.
     *
     * The client keeps a pointer to the interior instance the camera is in (null when outdoors). That
     * instance points to its root, which carries an inline NUL-terminated path. Reads both hops with
     * null checks so an outdoor camera or a half-built instance yields null rather than a fault.
     * @return the root path, or null when the camera is not inside any map-object.
     */
    inline const char* GetCurrentInteriorPath()
    {
        void* instance = Read<void*>(off::kCurrentInteriorInstance);
        if (!instance)
            return nullptr;
        void* root = At<void*>(instance, off::kOffInstanceRoot);
        if (!root)
            return nullptr;
        return static_cast<char*>(root) + off::kOffNameInline;
    }

    /**
     * @brief Reads the live outdoor-render gate value (a WMO/world global, not a per-root/group field).
     *
     * A negative value means the exterior pass is suppressed; the client checks this before drawing
     * outdoor-visible geometry while the camera is inside a map-object.
     * @return the current gate value.
     */
    inline float GetOutdoorGateValue()
    {
        return Read<float>(off::kOutdoorEnabled);
    }

    /**
     * @brief Resolves a MOBA batch record's material index, handling the legacy/modern layout split.
     * @param mobaRecord  the raw MOBA batch record.
     * @return the resolved material index.
     */
    inline uint32_t GetBatchMaterialIndex(const void* mobaRecord)
    {
        const auto* rec = static_cast<const uint8_t*>(mobaRecord);
        if (rec[off::kOffMobaFlags] & off::kMobaFlagMaterialModern)
        {
            uint16_t m;
            std::memcpy(&m, rec + off::kOffMobaMaterialModern, 2);
            return m;
        }
        return rec[off::kOffMobaMaterial];
    }
}
