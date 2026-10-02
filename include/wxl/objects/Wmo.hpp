// Wmo and WmoGroup: handles over one map-object root and over one of its groups -- the raw chunk
// buffers the native walk reads, and the root's group array. These are the pointers the AssetScript
// hooks hand an extension: OnWmoRootLoad carries the root as `root`, OnWmoGroupLoad carries the
// group as `group`. Read through wxl::game::wmo.
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

#include "wxl/game/Wmo.hpp"

namespace wxl
{
    /// One map-object group: the raw sub-chunk buffer the native sub-chunk walk reads.
    class WmoGroup
    {
    public:
        WmoGroup() = default;

        /**
         * Wraps a raw map-object group pointer.
         *
         * @param void* raw : the group object, may be null
         */
        explicit WmoGroup(void* raw) : raw_(raw) {}

        /// True when the handle holds a group.
        explicit operator bool() const { return raw_ != nullptr; }

        /**
         * The raw client pointer, for the bindings that take one.
         *
         * @return void* raw
         */
        void* Raw() const { return raw_; }

        /**
         * The group's sub-chunk buffer.
         *
         * @return void* buffer : null for a null handle
         */
        void* Buffer() const { return game::wmo::WmoGroup(raw_).GetBuffer(); }

        /**
         * The group buffer's byte size, the bound the native sub-chunk walk reads to.
         *
         * @return uint32 size : 0 for a null handle
         */
        uint32_t Size() const { return game::wmo::WmoGroup(raw_).GetSize(); }

        /**
         * Sets the group buffer's byte size, after reshaping the buffer in place.
         *
         * @param uint32 size : the new bound the native sub-chunk walk reads to
         */
        void SetSize(uint32_t size) { game::wmo::WmoGroup(raw_).SetSize(size); }

        bool operator==(const WmoGroup& o) const { return raw_ == o.raw_; }
        bool operator!=(const WmoGroup& o) const { return raw_ != o.raw_; }

    private:
        void* raw_ = nullptr;
    };

    /// One map-object root: the raw chunk buffer the native chunk walk reads, and its groups.
    class Wmo
    {
    public:
        Wmo() = default;

        /**
         * Wraps a raw map-object root pointer.
         *
         * @param void* raw : the root object, may be null
         */
        explicit Wmo(void* raw) : raw_(raw) {}

        /// True when the handle holds a root.
        explicit operator bool() const { return raw_ != nullptr; }

        /**
         * The raw client pointer, for the bindings that take one.
         *
         * @return void* raw
         */
        void* Raw() const { return raw_; }

        /**
         * The root's chunk buffer.
         *
         * @return void* buffer : null for a null handle
         */
        void* Buffer() const { return game::wmo::WmoRoot(raw_).GetBuffer(); }

        /**
         * The root buffer's byte size, the bound the native chunk walk reads to.
         *
         * @return uint32 size : 0 for a null handle
         */
        uint32_t Size() const { return game::wmo::WmoRoot(raw_).GetSize(); }

        /**
         * Sets the root buffer's byte size, after reshaping the buffer in place.
         *
         * @param uint32 size : the new bound the native chunk walk reads to
         */
        void SetSize(uint32_t size) { game::wmo::WmoRoot(raw_).SetSize(size); }

        /**
         * The root's group count, the bound of its group array.
         *
         * @return uint32 count : 0 for a null handle
         */
        uint32_t GroupCount() const { return game::wmo::WmoRoot(raw_).GetGroupCount(); }

        /**
         * The group at an index into the root's group array.
         *
         * @param uint32 i : the group index
         * @return WmoGroup group : null when out of range or for a null handle
         */
        WmoGroup GroupAt(uint32_t i) const { return WmoGroup(game::wmo::WmoRoot(raw_).GetGroupAt(i)); }

        /**
         * The resident state of one group, optionally forcing it resident.
         *
         * @param uint32 groupIndex : the group index
         * @param uint32 force : nonzero forces the group resident
         * @return uint32 resident : the group's resident state
         */
        unsigned GroupResident(unsigned groupIndex, unsigned force) const
        { return game::wmo::WmoRoot(raw_).GetGroupResident(groupIndex, force); }

        /**
         * Resolves one material's texture-name offsets. The native does not bounds-check the index.
         *
         * @param int32 materialIndex : the material to resolve
         */
        void ResolveMaterialTexture(int materialIndex)
        { game::wmo::WmoRoot(raw_).ResolveMaterialTexture(materialIndex); }

        /**
         * The inline file path of the map-object the camera is currently inside. Camera state, not a
         * field of this handle: it ignores the root it is called on.
         *
         * @return string path : null when the camera is outdoors or the instance is half-built
         */
        static const char* CurrentInteriorPath() { return game::wmo::GetCurrentInteriorPath(); }

        /**
         * The live outdoor-render gate value, a world global rather than a field of this handle: it
         * ignores the root it is called on. The client reads it before drawing outdoor-visible
         * geometry while the camera is inside a map-object.
         *
         * @return float gate : negative suppresses the exterior pass
         */
        static float OutdoorGateValue() { return game::wmo::GetOutdoorGateValue(); }

        bool operator==(const Wmo& o) const { return raw_ == o.raw_; }
        bool operator!=(const Wmo& o) const { return raw_ != o.raw_; }

    private:
        void* raw_ = nullptr;
    };
}
