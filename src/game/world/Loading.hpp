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
