// world loading bindings: tick and load-gate, async-I/O queue pumps, and load-state globals.
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
#include "offsets/game/World.hpp"

/**
 * @brief Typed inline wrappers over the world tick, async-I/O queue pumps, and load-state globals.
 */
namespace wxl::game::world
{
    namespace woff = wxl::offsets::game::world;

    /**
     * @brief Runs one world tick plus the loading-screen synchronous drain.
     * @param param  Tick parameter passed through to the native call.
     */
    inline void Tick(int param)
    {
        Native<woff::World_TickFn>(woff::kTick)(param);
    }

    /**
     * @brief Reads the numeric map id of the loaded world.
     * @return The current map id, or -1 when no world is loaded.
     */
    inline int32_t MapId()
    {
        return *reinterpret_cast<int32_t*>(woff::kCurrentMapId);
    }

    /**
     * @brief Changes the terrain source to another map directory at the current position (CMap::Enter).
     *
     * Runs the engine's own map-change: repoints the dir/name/wdt-path globals to @p mapDir, purges all
     * tiles, loads that map's WDT + WDL, and re-streams around the current camera. The player is not moved.
     * @param mapDir  the map directory stem (e.g. "Azeroth", "Gilneas").
     * @param mapId   the map id to keep (pass MapId() to preserve the logical map / gameplay).
     */
    inline void EnterMap(const char* mapDir, int mapId)
    {
        Native<woff::World_MapEnterFn>(woff::kMapEnter)(mapDir, mapId);
    }

    /**
     * @brief Re-streams the loaded terrain tiles in place (no map reload). A dev lever to pick up changed
     *        served bytes without relogging; the terrain phase does NOT use this (it re-enters via EnterMap).
     *
     * Reopens each idle tile's load gate (zero +0x80 / +0x84) so the streamer re-queues its read on the next
     * ticks, keeping the tile object + its chunk pointers valid until the fresh read finalizes - rather than a
     * mass purge, which would make the streamer touch a not-yet-finalized tile. Drains in-flight reads first.
     */
    inline void RequestTerrainReload()
    {
        Native<woff::World_AsyncWaitAllFn>(woff::kAsyncWaitAll)();

        uint32_t node = *reinterpret_cast<uint32_t*>(woff::kActiveListHead);
        if ((node & 1) || node == 0) return;
        const uint32_t linkBase = *reinterpret_cast<uint32_t*>(woff::kActiveListLinkBase);
        for (int guard = 0; (node & 1) == 0 && node != 0 && guard < 100000; ++guard)
        {
            const uint32_t tile = *reinterpret_cast<uint32_t*>(node + 4);
            const uint32_t next = *reinterpret_cast<uint32_t*>(linkBase + 4 + node);
            if (tile >= 0x00400000 && tile < 0xF0000000 &&
                *reinterpret_cast<uint32_t*>(tile + woff::kOffTileAsyncRead) == 0) // idle: no read in flight
            {
                *reinterpret_cast<uint32_t*>(tile + woff::kOffTileFileBuffer) = 0; // reopen the load gate
                *reinterpret_cast<uint32_t*>(tile + woff::kOffTileFileId)     = 0;
            }
            node = next;
        }
    }

    /**
     * @brief Activates a terrain phase: overlays a phase map's variant tiles onto the current base map.
     *
     * Reads the phase map's WDT present table to learn which tiles it has, then re-streams the base map so
     * the per-tile loader serves those tiles from the phase directory (same coords) while the rest stay base.
     * @param childMapDir  the phase map directory (e.g. "Gilneas"); null/empty is ignored (use ClearTerrainPhase).
     */
    void SetTerrainPhase(const char* childMapDir);

    /** @brief Clears the active terrain phase (loaded tiles return to the base map on the next re-stream). */
    void ClearTerrainPhase();

    /**
     * @brief Reads the loaded map's bare name (the stem of the served ADT files), e.g. "Azeroth".
     * @return Pointer to the engine's map-name string (empty before a world is loaded).
     */
    inline const char* MapName()
    {
        return reinterpret_cast<const char*>(woff::kMapNameStr);
    }

    /**
     * @brief Computes the ADT tile the focus position (player) sits on, in the filename's index order.
     * @param first   Receives the first %d of <Map>_<first>_<second>.adt.
     * @param second  Receives the second %d.
     */
    inline void CurrentTile(int& first, int& second)
    {
        const float px = *reinterpret_cast<float*>(woff::kFocusPosX);
        const float py = *reinterpret_cast<float*>(woff::kFocusPosY);
        first  = static_cast<int>((woff::kGridOriginYards - py) / woff::kTileSizeYards);
        second = static_cast<int>((woff::kGridOriginYards - px) / woff::kTileSizeYards);
    }

    /** @brief Pumps the async queues, blocking until no async file read is pending. */
    inline void AsyncWaitAll()
    {
        Native<woff::World_AsyncWaitAllFn>(woff::kAsyncWaitAll)();
    }

    /**
     * @brief Reports whether any async file request still has outstanding work.
     * @return True while an async request is pending.
     */
    inline bool AsyncPending()
    {
        return Native<woff::World_AsyncPendingFn>(woff::kAsyncPending)() != 0;
    }

    /** @brief Services the async queues for one pump. */
    inline void AsyncServiceQueues()
    {
        Native<woff::World_AsyncServiceQueuesFn>(woff::kAsyncServiceQueues)(0, 0);
    }

    /**
     * @brief Reports whether the blocking load-gate drain is running.
     * @return True while the load-gate drain runs.
     */
    inline bool LoadActive()
    {
        return *reinterpret_cast<uint32_t*>(woff::kLoadActive) != 0;
    }

    /**
     * @brief Reads the focus world position (center of the load box / player position).
     * @param out  Receives the position in out[0..2].
     */
    inline void FocusPos(float out[3])
    {
        out[0] = *reinterpret_cast<float*>(woff::kFocusPosX);
        out[1] = *reinterpret_cast<float*>(woff::kFocusPosY);
        out[2] = *reinterpret_cast<float*>(woff::kFocusPosZ);
    }

    /** @brief Adds the world-loading bindings to the enumerable catalog. */
    inline void RegisterCatalog()
    {
        Register({ "World::Tick",               woff::kTick,               "void(int param)" });
        Register({ "World::AsyncWaitAll",        woff::kAsyncWaitAll,        "void()" });
        Register({ "World::AsyncPending",        woff::kAsyncPending,        "bool()" });
        Register({ "World::AsyncServiceQueues",  woff::kAsyncServiceQueues,  "void()" });
    }
}
