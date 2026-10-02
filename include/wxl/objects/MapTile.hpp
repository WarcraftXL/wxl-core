// MapTile: a handle over one resident map tile, a slot of the client's 64x64 tile grid. One pointer,
// owns nothing. The slot lookup goes through the wxl::game::adt binding; the tile's own fields are
// read through the offset-checked tile-area view.
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

#include "wxl/game/Adt.hpp"
#include "wxl/offsets/game/ADT.hpp"

namespace wxl
{
    /// A resident map tile: one "<Map>_%d_%d.adt" of the 64x64 grid, named by the two numbers of its
    /// file name. The handle is thin: the client exposes the tile's file state and its two filename
    /// numbers, and the terrain itself is reached through MapChunk.
    class MapTile
    {
    public:
        /// Tiles per axis of the grid, so both filename numbers are 0..kGridDim-1.
        static constexpr uint32_t kGridDim = offsets::game::adt::kTileGridDim;

        MapTile() = default;

        /**
         * Wraps a raw tile pointer.
         *
         * @param void* raw : the client tile, may be null
         */
        explicit MapTile(void* raw) : raw_(raw) {}

        /**
         * The tile at a grid slot. The slot index is tileSecond * 64 + tileFirst, so the SECOND
         * filename number selects the row: for "Azeroth_31_48.adt", tileFirst is 31 and tileSecond
         * is 48. Passing them the other way round reads a different, usually resident, tile.
         *
         * @param uint32 tileSecond : second %d of "<Map>_%d_%d.adt", the row of the grid walk
         * @param uint32 tileFirst : first %d of "<Map>_%d_%d.adt"
         * @return MapTile tile : null when out of range or not resident
         */
        static MapTile Slot(uint32_t tileSecond, uint32_t tileFirst)
        {
            return MapTile(game::adt::TileSlot(tileSecond, tileFirst));
        }

        /// True when the handle holds a tile, which means that grid slot is resident.
        explicit operator bool() const { return raw_ != nullptr; }

        /**
         * The raw client pointer, for the bindings that take one.
         *
         * @return void* raw
         */
        void* Raw() const { return raw_; }

        /**
         * The first %d of the tile's "<Map>_%d_%d.adt" file name.
         *
         * @return int32 tileFirst : -1 for a null handle
         */
        int32_t TileFirst() const { return raw_ ? View()->tileFirst : -1; }

        /**
         * The second %d of the tile's "<Map>_%d_%d.adt" file name, the row of the grid.
         *
         * @return int32 tileSecond : -1 for a null handle
         */
        int32_t TileSecond() const { return raw_ ? View()->tileSecond : -1; }

        /**
         * The byte size of the tile's raw file buffer.
         *
         * @return uint32 size : 0 for a null handle or before the buffer is allocated
         */
        uint32_t FileSize() const { return raw_ ? View()->fileSize : 0u; }

        /// True when the tile's raw file buffer is allocated.
        bool HasFileData() const { return raw_ && View()->fileBuffer != 0u; }

        /// True while the tile's whole-file async read is still in flight.
        bool IsReading() const { return raw_ && View()->asyncRead != 0u; }

        bool operator==(const MapTile& t) const { return raw_ == t.raw_; }
        bool operator!=(const MapTile& t) const { return raw_ != t.raw_; }

    protected:
        /// The typed view over the tile object, every field offset checked at compile time.
        const offsets::game::adt::TileArea* View() const
        {
            return static_cast<const offsets::game::adt::TileArea*>(raw_);
        }

        void* raw_ = nullptr;
    };
}
