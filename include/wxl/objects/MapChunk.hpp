// MapChunk: a handle over one runtime terrain chunk, the object the client parses a tile's MCNK into.
// One pointer, owns nothing, reads through the wxl::game::adt and wxl::game::doodad bindings, and
// walks the chunk's placed-doodad list.
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

#include "wxl/game/Adt.hpp"
#include "wxl/game/Doodad.hpp"
#include "wxl/objects/Doodad.hpp"
#include "wxl/objects/Object.hpp"

namespace wxl
{
    /// A runtime terrain chunk: the heightmap and collision the client resolves a world position to,
    /// and the list of doodads placed on it.
    class MapChunk
    {
    public:
        /// Doodads one ForEachDoodad pass can visit. The enumeration restarts at the list head on
        /// every call, so this is a hard ceiling, not a window that advances.
        static constexpr int kDoodadBatch = 256;

        MapChunk() = default;

        /**
         * Wraps a raw chunk pointer.
         *
         * @param void* raw : the client chunk, may be null
         */
        explicit MapChunk(void* raw) : raw_(raw) {}

        /**
         * The chunk under a world position. Goes through game::adt::GetChunk, the terrain catalog's
         * entry; game::doodad::ChunkAt reaches the same client function.
         *
         * @param Vec3 pos : the world position to look under
         * @return MapChunk chunk : null when that chunk is not parsed yet
         */
        static MapChunk At(const Vec3& pos)
        {
            float p[3] = { pos.x, pos.y, pos.z };
            return MapChunk(game::adt::GetChunk(p));
        }

        /**
         * The chunk under a world position given as a coordinate triple.
         *
         * @param float pos : the world position in pos[0..2]
         * @return MapChunk chunk : null when that chunk is not parsed yet
         */
        static MapChunk At(const float pos[3])
        {
            float p[3] = { pos[0], pos[1], pos[2] };
            return MapChunk(game::adt::GetChunk(p));
        }

        /// True when the handle holds a chunk, which means its heightmap and collision are resident.
        explicit operator bool() const { return raw_ != nullptr; }

        /**
         * The raw client pointer, for the bindings that take one.
         *
         * @return void* raw
         */
        void* Raw() const { return raw_; }

        /**
         * Collects the chunk's placed doodads into a caller array. Creatures never appear: only placed
         * map doodads are on this list.
         *
         * @param void** out : receives the doodad pointers
         * @param int maxCount : capacity of out
         * @return int count : how many pointers were written
         */
        int Doodads(void** out, int maxCount) const
        {
            return game::doodad::EnumerateChunk(raw_, out, 0, maxCount);
        }

        /**
         * Calls fn once per placed doodad of the chunk, with a Doodad handle. Visits at most
         * kDoodadBatch doodads.
         *
         * @param Fn fn : callable taking a Doodad
         * @return int count : how many doodads were visited
         */
        template <class Fn>
        int ForEachDoodad(Fn&& fn) const
        {
            void* batch[kDoodadBatch];
            const int n = game::doodad::EnumerateChunk(raw_, batch, 0, kDoodadBatch);
            for (int i = 0; i < n; ++i) fn(Doodad(batch[i]));
            return n;
        }

        /**
         * Counts the placed-object children overlapping the chunk box that are still loading.
         * total is not defaulted: the binding gives no neutral value for it.
         *
         * @param int* progressOut : receives the loaded-object progress count
         * @param int total : total object count to measure the progress against
         * @return int count : children still loading, 0 for a null handle
         */
        int NearObjectCount(int* progressOut, int total) const
        {
            return raw_ ? game::adt::NearObjectCount(raw_, progressOut, total) : 0;
        }

        /**
         * The same count, with the progress written to a local the caller does not need to declare.
         *
         * @param int total : total object count to measure the progress against
         * @return int count : children still loading, 0 for a null handle
         */
        int NearObjectCount(int total) const
        {
            int progress = 0;
            return NearObjectCount(&progress, total);
        }

        bool operator==(const MapChunk& c) const { return raw_ == c.raw_; }
        bool operator!=(const MapChunk& c) const { return raw_ != c.raw_; }

    protected:
        void* raw_ = nullptr;
    };
}
