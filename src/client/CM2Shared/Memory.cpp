// Dedicated M2 buffer arena: a reserved 32-bit VA region for large model buffers, published to wxl-m2.
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

// Only the arena RESERVATION lives here now (Boot phase, on the loader thread, before world loading
// fragments the 32-bit VA space -- earlier than any extension can exist). The allocator detours that
// route large M2 buffers into it (and the standalone-VirtualAlloc fallback) moved to wxl-m2, which
// reaches this arena through the "wxl.m2arena" interface published below -- see include/wxl/M2ArenaApi.h.

#include "engine/hook/Hook.hpp"
#include "engine/hook/Registry.hpp"
#include "runtime/Extensions.hpp"

#include "common/Config.hpp"
#include "common/Log.hpp"
#include "wxl/M2ArenaApi.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <vector>

namespace
{
    // Reserve only what the observed city workload actually needs. A 256 MB reservation stranded roughly
    // 190 MB of scarce 32-bit VA while the model loader later failed to find a separate 15 MB contiguous block.
    constexpr uint32_t kDefaultM2ArenaSizeMb = 128u;

    struct M2ArenaRange
    {
        uint32_t offset = 0;
        uint32_t size = 0;
    };

    uint32_t AlignUpU32(uint32_t value, uint32_t align)
    {
        return (value + align - 1u) & ~(align - 1u);
    }

    std::mutex g_arenaMutex;
    uint8_t* g_m2ArenaBase = nullptr;
    uint32_t g_m2ArenaSize = 0;
    std::vector<M2ArenaRange> g_m2ArenaFree;

    /** @brief Logs a coarse 32-bit address-space snapshot around very large model allocations. */
    void LogClientAddressSpace(const char* reason)
    {
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        uintptr_t address = reinterpret_cast<uintptr_t>(info.lpMinimumApplicationAddress);
        const uintptr_t maximum = reinterpret_cast<uintptr_t>(info.lpMaximumApplicationAddress);
        uint64_t committed = 0, reserved = 0, freeBytes = 0, largestFree = 0;
        while (address < maximum)
        {
            MEMORY_BASIC_INFORMATION mbi{};
            if (!VirtualQuery(reinterpret_cast<const void*>(address), &mbi, sizeof(mbi)) || !mbi.RegionSize)
                break;
            const uint64_t bytes = static_cast<uint64_t>(mbi.RegionSize);
            if (mbi.State == MEM_COMMIT) committed += bytes;
            else if (mbi.State == MEM_RESERVE) reserved += bytes;
            else if (mbi.State == MEM_FREE)
            {
                freeBytes += bytes;
                largestFree = std::max(largestFree, bytes);
            }
            const uintptr_t next = address + static_cast<uintptr_t>(mbi.RegionSize);
            if (next <= address) break;
            address = next;
        }

        uint64_t arenaFree = 0, arenaLargest = 0;
        {
            std::lock_guard<std::mutex> lock(g_arenaMutex);
            for (const M2ArenaRange& range : g_m2ArenaFree)
            {
                arenaFree += range.size;
                arenaLargest = std::max<uint64_t>(arenaLargest, range.size);
            }
        }
        WLOG_INFO(
            "client-memory: reason=%s commit_mb=%.1f reserve_mb=%.1f free_mb=%.1f largest_free_mb=%.1f arena_free_mb=%.1f arena_largest_mb=%.1f",
            reason ? reason : "unknown",
            static_cast<double>(committed) / (1024.0 * 1024.0),
            static_cast<double>(reserved) / (1024.0 * 1024.0),
            static_cast<double>(freeBytes) / (1024.0 * 1024.0),
            static_cast<double>(largestFree) / (1024.0 * 1024.0),
            static_cast<double>(arenaFree) / (1024.0 * 1024.0),
            static_cast<double>(arenaLargest) / (1024.0 * 1024.0));
    }

    uint32_t M2ArenaSizeMb()
    {
        static const uint32_t mb =
            wxl::config::U32("WXL_M2_ARENA_MB", kDefaultM2ArenaSizeMb, 64, 2048);
        return mb;
    }

    bool M2ArenaEnabled()
    {
        static const bool enabled = wxl::config::Flag("WXL_M2_ARENA", "WarcraftXL_m2_arena.disable");
        return enabled;
    }

    bool EnsureM2ArenaLocked()
    {
        if (g_m2ArenaBase)
            return true;
        if (!M2ArenaEnabled())
            return false;

        const uint32_t mb = M2ArenaSizeMb();
        const uint64_t bytes64 = static_cast<uint64_t>(mb) * 1024u * 1024u;
        if (bytes64 > 0xffffffffu)
            return false;

        auto* base = static_cast<uint8_t*>(
            VirtualAlloc(nullptr, static_cast<SIZE_T>(bytes64), MEM_RESERVE, PAGE_READWRITE));
        if (!base)
        {
            WLOG_WARN("m2-memory: failed to reserve %u MB arena", mb);
            return false;
        }

        g_m2ArenaBase = base;
        g_m2ArenaSize = static_cast<uint32_t>(bytes64);
        g_m2ArenaFree.clear();
        g_m2ArenaFree.push_back({ 0, g_m2ArenaSize });
        WLOG_INFO("m2-memory: reserved %u MB large-model arena", mb);
        return true;
    }

    bool CommitArenaRange(uint32_t offset, uint32_t size)
    {
        if (!g_m2ArenaBase || size == 0)
            return false;
        return VirtualAlloc(g_m2ArenaBase + offset, size, MEM_COMMIT, PAGE_READWRITE) != nullptr;
    }

    // Inserts a range back into the free list and coalesces neighbours. List surgery only -- the
    // caller decommits (outside the mutex) when the range's pages were actually committed.
    void InsertArenaFreeRangeLocked(uint32_t offset, uint32_t size)
    {
        if (!g_m2ArenaBase || size == 0)
            return;

        M2ArenaRange r{ offset, size };
        auto it = g_m2ArenaFree.begin();
        while (it != g_m2ArenaFree.end() && it->offset < offset)
            ++it;
        it = g_m2ArenaFree.insert(it, r);

        if (it != g_m2ArenaFree.begin())
        {
            auto prev = it - 1;
            if (prev->offset + prev->size == it->offset)
            {
                prev->size += it->size;
                it = g_m2ArenaFree.erase(it);
                it = prev;
            }
        }

        auto next = it + 1;
        if (next != g_m2ArenaFree.end() && it->offset + it->size == next->offset)
        {
            it->size += next->size;
            g_m2ArenaFree.erase(next);
        }
    }

    /**
     * @brief Reserves a first-fit range from the arena free list. Caller holds the mutex.
     *
     * Only the free-list surgery happens here; the MEM_COMMIT (a kernel call that zero-fills
     * megabytes) is the caller's job, outside the mutex, so concurrent loader threads and the
     * main thread's force-wait path stop serializing behind each other's page commits.
     * @param need       page-aligned byte count to reserve.
     * @param offsetOut  receives the reserved arena offset.
     * @return true when a range was reserved.
     */
    bool ReserveArenaRangeLocked(uint32_t need, uint32_t& offsetOut)
    {
        if (!EnsureM2ArenaLocked())
            return false;

        for (size_t i = 0; i < g_m2ArenaFree.size(); ++i)
        {
            const M2ArenaRange range = g_m2ArenaFree[i];
            if (range.size < need)
                continue;

            if (need == range.size)
            {
                g_m2ArenaFree.erase(g_m2ArenaFree.begin() + static_cast<ptrdiff_t>(i));
            }
            else
            {
                g_m2ArenaFree[i].offset = range.offset + need;
                g_m2ArenaFree[i].size = range.size - need;
            }
            offsetOut = range.offset;
            return true;
        }
        return false;
    }

    // ------------------------------------------------------------------ wxl.m2arena
    void* __cdecl ApiAlloc(uint32_t size, uint32_t* outOffset, uint32_t* outSize)
    {
        const uint32_t need = AlignUpU32(size + 0x20u, 0x1000u);
        uint32_t offset = 0;
        bool reserved;
        {
            std::lock_guard<std::mutex> lock(g_arenaMutex);
            reserved = ReserveArenaRangeLocked(need, offset);
        }
        if (!reserved)
            return nullptr;

        // Commit outside the mutex; VirtualAlloc either commits the whole range or fails without
        // committing, so a failure just returns the reserved range to the list.
        if (!CommitArenaRange(offset, need))
        {
            std::lock_guard<std::mutex> lock(g_arenaMutex);
            InsertArenaFreeRangeLocked(offset, need);
            return nullptr;
        }

        auto* ptr = g_m2ArenaBase + offset + 0x10u;
        ptr[-1] = 0x10u;
        if (outOffset) *outOffset = offset;
        if (outSize) *outSize = need;
        return ptr;
    }

    void __cdecl ApiFree(uint32_t offset, uint32_t size)
    {
        if (!g_m2ArenaBase || size == 0)
            return;
        // Decommit outside the mutex (kernel call), then give the range back to the free list.
        VirtualFree(g_m2ArenaBase + offset, size, MEM_DECOMMIT);
        std::lock_guard<std::mutex> lock(g_arenaMutex);
        InsertArenaFreeRangeLocked(offset, size);
    }

    void __cdecl ApiLogAddressSpace(const char* reason)
    {
        LogClientAddressSpace(reason);
    }

    WXL_M2ArenaApi g_m2ArenaApi = {
        sizeof(WXL_M2ArenaApi), WXL_M2ARENA_API_VERSION, &ApiAlloc, &ApiFree, &ApiLogAddressSpace,
    };

    /**
     * @brief Boot-phase reservation of the large-M2 arena before world loading fragments the VA space.
     *
     * Grabs the contiguous 32-bit reservation early, on the loader thread, so a later large model
     * buffer finds room; deferring it to the first allocation would race the client's own world-load
     * allocations for the same scarce address space. Best-effort: a disabled arena or a failed reserve
     * is not fatal (large buffers then fall back to a standalone VirtualAlloc or the native allocator).
     * Publishes "wxl.m2arena" regardless of outcome, so wxl-m2's allocator detours always find the
     * interface -- Alloc simply returns null when the arena never came up.
     */
    bool ReserveM2Arena()
    {
        {
            std::lock_guard<std::mutex> lock(g_arenaMutex);
            EnsureM2ArenaLocked();
        }
        wxl::runtime::extensions::PublishInterface("wxl.m2arena", WXL_M2ARENA_API_VERSION, &g_m2ArenaApi);
        return true;
    }
}

// Always registered: the arena's own WXL_M2_ARENA config knob decides whether anything is actually
// reserved (see M2ArenaEnabled() above), and wxl.m2arena must exist so wxl-m2 always finds it,
// regardless of that extension's own kEnabled -- the interface being null-Alloc-only costs nothing.
WXL_REGISTER_FEATURE_PHASED("m2memory-arena", true, ReserveM2Arena, ::wxl::hook::Phase::Boot)
