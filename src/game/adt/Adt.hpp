// ADT game bindings: typed inline wrappers over the client's terrain functions.
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
#include "offsets/game/ADT.hpp"

/**
 * @brief Typed inline wrappers over the client terrain functions, exposed as the ADT binding catalog.
 */
namespace wxl::game::adt
{
    namespace off = wxl::offsets::game::adt;

    /**
     * @brief Looks up the runtime chunk object at a world position.
     * @param pos  World-space position pointer.
     * @return The chunk object, or null when that chunk is not parsed yet.
     */
    inline void* GetChunk(float* pos)
    {
        return Native<off::Map_GetChunkFn>(off::kGetChunk)(pos);
    }

    /**
     * @brief Counts placed-object children still loading that overlap the chunk box.
     * @param chunk        Chunk object to test.
     * @param progressOut  Receives the loaded-object progress count.
     * @param total        Total object count to measure progress against.
     * @return The count of overlapping children still loading.
     */
    inline int NearObjectCount(void* chunk, int* progressOut, int total)
    {
        return Native<off::Map_NearObjectCountFn>(off::kNearObjectCount)(chunk, progressOut, total);
    }

    /**
     * @brief Reads a tile-slot pointer from the 64x64 tile grid.
     *
     * The true slot index is secondFilenameNumber * 64 + firstFilenameNumber, where the numbers are
     * the "%d_%d" of the "<Map>_%d_%d.adt" tile name (area+0x48 first, area+0x4C second). The old
     * parameter names (tileX/tileY) matched that formula only under a swapped naming where "tileX"
     * meant the second number; the parameters are renamed to the filename semantics, formula unchanged.
     * @param tileSecond  Second %d of the tile filename (the row of the grid walk).
     * @param tileFirst   First %d of the tile filename.
     * @return The tile-area pointer, or null when out of range / not resident.
     */
    inline void* TileSlot(uint32_t tileSecond, uint32_t tileFirst)
    {
        if (tileSecond >= off::kTileGridDim || tileFirst >= off::kTileGridDim)
            return nullptr;
        return *reinterpret_cast<void**>(off::kTileSlots + (tileSecond * off::kTileGridDim + tileFirst) * off::kTileSlotStride);
    }

    /** @brief Adds the ADT bindings to the enumerable catalog. */
    inline void RegisterCatalog()
    {
        Register({ "ADT::GetChunk",        off::kGetChunk,        "void*(float* pos)" });
        Register({ "ADT::NearObjectCount", off::kNearObjectCount, "int(chunk, int* progress, int total)" });
    }
}
