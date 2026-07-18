// Background pre-warming: resolver tables and neighbor map-tile transforms, off the client request thread.
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

#include <string>

// Below-normal-priority pre-warming so cold work lands before the client reaches it: the lazy FileDataID
// resolver tables at startup, and the neighbours of each served map tile during flight. Both run on their
// own threads and never block a live request; the tile warmer feeds produced bytes into the transform cache.
namespace wxl::host::warm
{
    /** @brief Warms the lazy FileDataID resolver tables on a below-normal-priority background thread. */
    void StartResolverWarmer();

    /** @brief Starts the below-normal-priority worker that produces queued neighbor tiles into the cache. */
    void StartTileWarmer();

    /**
     * @brief Queues the 3x3 neighborhood of a just-served tile for background pre-transform.
     *
     * Flying across a continent opens map tiles at a steady spatial rhythm, and a cold tile is the single
     * most expensive host transform (the micro-freeze the player feels). Producing the eight neighbors
     * ahead of time keeps the transform cache warm when the client reaches them. A non-tile name is ignored.
     * @param servedName  the tile name that was just served
     */
    void QueueNeighborTiles(const std::string& servedName);
}
