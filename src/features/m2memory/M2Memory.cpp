// Dedicated M2 buffer arena: a reserved 32-bit VA region for large model buffers, with a standalone fallback.
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

#include "config.hpp"
#include "engine/hook/Hook.hpp"
#include "engine/hook/Registry.hpp"

#include "common/Config.hpp"
#include "common/Log.hpp"
#include "offsets/game/M2.hpp"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace
{
    namespace m2 = wxl::offsets::game::m2;

    m2::M2_BufferAllocFn g_origM2BufferAlloc = nullptr;
    m2::M2_BufferFreeFn  g_origM2BufferFree  = nullptr;

    constexpr uint32_t kDefaultVirtualM2AllocThreshold = 1u * 1024u * 1024u;
    // Reserve only what the observed city workload actually needs. A 256 MB reservation stranded roughly
    // 190 MB of scarce 32-bit VA while CM2Model later failed to find a separate 15 MB contiguous block.
    constexpr uint32_t kDefaultM2ArenaSizeMb = 128u;

    struct VirtualM2Allocation
    {
        void* base = nullptr;      // non-null for standalone VirtualAlloc
        uint32_t arenaOffset = 0;  // valid when base == nullptr
        uint32_t arenaSize = 0;
    };

    struct M2ArenaRange
    {
        uint32_t offset = 0;
        uint32_t size = 0;
    };

    uint32_t AlignUpU32(uint32_t value, uint32_t align)
    {
        return (value + align - 1u) & ~(align - 1u);
    }

    std::mutex g_virtualM2AllocMutex;
    std::unordered_map<void*, VirtualM2Allocation> g_virtualM2Allocs;
    uint8_t* g_m2ArenaBase = nullptr;
    uint32_t g_m2ArenaSize = 0;
    std::vector<M2ArenaRange> g_m2ArenaFree;
    std::atomic<uint32_t> g_duplicateM2ArenaFrees{ 0 };

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
            std::lock_guard<std::mutex> lock(g_virtualM2AllocMutex);
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

    bool LargeM2VirtualAllocEnabled()
    {
        static const bool enabled =
            wxl::config::Flag("WXL_M2_VIRTUAL_ALLOC", "WarcraftXL_m2_virtual_alloc.disable");
        return enabled;
    }

    uint32_t VirtualM2AllocThreshold()
    {
        static const uint32_t bytes = wxl::config::BytesMbKb(
            "WXL_M2_VIRTUAL_ALLOC_THRESHOLD_MB", "WXL_M2_VIRTUAL_ALLOC_THRESHOLD_KB",
            kDefaultVirtualM2AllocThreshold, 64, 2048u * 1024u);
        return bytes;
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

    // Inserts a range back into the free list and coalesces neighbours. List surgery only — the
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

    void* TryVirtualM2Alloc(uint32_t size)
    {
        const SIZE_T total = static_cast<SIZE_T>(size) + 0x20u;
        auto* base = static_cast<uint8_t*>(VirtualAlloc(nullptr, total, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        if (!base)
            return nullptr;

        const uintptr_t aligned = (reinterpret_cast<uintptr_t>(base) + 0x1Fu) & ~uintptr_t(0x0Fu);
        auto* ptr = reinterpret_cast<uint8_t*>(aligned);
        const uintptr_t shift = reinterpret_cast<uintptr_t>(ptr) - reinterpret_cast<uintptr_t>(base);
        if (shift == 0 || shift > 0xFF)
        {
            VirtualFree(base, 0, MEM_RELEASE);
            return nullptr;
        }
        ptr[-1] = static_cast<uint8_t>(shift);

        std::lock_guard<std::mutex> lock(g_virtualM2AllocMutex);
        g_virtualM2Allocs.emplace(ptr, VirtualM2Allocation{ base, 0, 0 });
        return ptr;
    }

    void __cdecl hkM2BufferFree(void* ptr)
    {
        if (!ptr)
            return;

        VirtualM2Allocation alloc{};
        bool ours = false;
        bool staleArenaPointer = false;
        {
            std::lock_guard<std::mutex> lock(g_virtualM2AllocMutex);
            auto it = g_virtualM2Allocs.find(ptr);
            if (it != g_virtualM2Allocs.end())
            {
                alloc = it->second;
                g_virtualM2Allocs.erase(it);
                ours = true;
            }

            // Blizzard's heap can never own a pointer in this exclusively reserved arena. Shared
            // collection models may be detached twice in one frame; once the first free removes our
            // allocation record, a duplicate must not fall through to the native heap free.
            if (!ours && g_m2ArenaBase && g_m2ArenaSize)
            {
                const uintptr_t address = reinterpret_cast<uintptr_t>(ptr);
                const uintptr_t begin = reinterpret_cast<uintptr_t>(g_m2ArenaBase);
                const uintptr_t end = begin + static_cast<uintptr_t>(g_m2ArenaSize);
                staleArenaPointer = address >= begin && address < end;
            }
        }

        if (ours && !alloc.base)
        {
            // Decommit outside the mutex (kernel call), then give the range back to the free list.
            VirtualFree(g_m2ArenaBase + alloc.arenaOffset, alloc.arenaSize, MEM_DECOMMIT);
            std::lock_guard<std::mutex> lock(g_virtualM2AllocMutex);
            InsertArenaFreeRangeLocked(alloc.arenaOffset, alloc.arenaSize);
            return;
        }

        if (ours && alloc.base)
        {
            VirtualFree(alloc.base, 0, MEM_RELEASE);
            return;
        }

        if (staleArenaPointer)
        {
            const uint32_t count = g_duplicateM2ArenaFrees.fetch_add(1, std::memory_order_relaxed) + 1;
            if (count == 1 || (count & (count - 1)) == 0)
                WLOG_WARN("m2-memory: ignored duplicate/stale arena free ptr=%p (count=%u)", ptr, count);
            return;
        }

        g_origM2BufferFree(ptr);
    }

    void* __cdecl hkM2BufferAlloc(uint32_t size, const char* tag, int line)
    {
        if (size >= VirtualM2AllocThreshold() && LargeM2VirtualAllocEnabled())
        {
            const uint32_t need = AlignUpU32(size + 0x20u, 0x1000u);
            uint32_t offset = 0;
            bool reserved = false;
            {
                std::lock_guard<std::mutex> lock(g_virtualM2AllocMutex);
                reserved = ReserveArenaRangeLocked(need, offset);
            }
            if (reserved)
            {
                // Commit outside the mutex; VirtualAlloc either commits the whole range or fails
                // without committing, so a failure just returns the reserved range to the list.
                if (CommitArenaRange(offset, need))
                {
                    auto* ptr = g_m2ArenaBase + offset + 0x10u;
                    ptr[-1] = 0x10u;
                    {
                        std::lock_guard<std::mutex> lock(g_virtualM2AllocMutex);
                        g_virtualM2Allocs.emplace(ptr, VirtualM2Allocation{ nullptr, offset, need });
                    }
                    WLOG_DEBUG("m2-memory: arena buffer %u bytes (%s)", size, tag ? tag : "M2");
                    if (size >= 8u * 1024u * 1024u) LogClientAddressSpace("m2-arena");
                    return ptr;
                }
                std::lock_guard<std::mutex> lock(g_virtualM2AllocMutex);
                InsertArenaFreeRangeLocked(offset, need);
            }

            if (void* standalone = TryVirtualM2Alloc(size))
            {
                WLOG_DEBUG("m2-memory: virtual buffer %u bytes (%s)", size, tag ? tag : "M2");
                if (size >= 8u * 1024u * 1024u) LogClientAddressSpace("m2-virtual");
                return standalone;
            }
            if (size >= 8u * 1024u * 1024u) LogClientAddressSpace("m2-virtual-failed");
            WLOG_WARN("m2-memory: VirtualAlloc failed for %u bytes, falling back to native allocator", size);
        }

        return g_origM2BufferAlloc(size, tag, line);
    }

    /**
     * @brief Boot-phase reservation of the large-M2 arena before world loading fragments the VA space.
     *
     * Grabs the contiguous 32-bit reservation early, on the loader thread, so a later large CM2Model
     * buffer finds room; deferring it to the first allocation would race the client's own world-load
     * allocations for the same scarce address space. Best-effort: a disabled arena or a failed reserve
     * is not fatal (large buffers then fall back to a standalone VirtualAlloc or the native allocator).
     */
    bool ReserveM2Arena()
    {
        std::lock_guard<std::mutex> lock(g_virtualM2AllocMutex);
        EnsureM2ArenaLocked();
        return true;
    }

    /**
     * @brief Normal-phase install of the M2 buffer allocator detours that route large buffers to the arena.
     */
    bool InstallM2Memory()
    {
        wxl::hook::Install("M2BufferAlloc", m2::kBufferAlloc, &hkM2BufferAlloc, &g_origM2BufferAlloc);
        wxl::hook::Install("M2BufferFree", m2::kBufferFree, &hkM2BufferFree, &g_origM2BufferFree);
        return true;
    }
}

WXL_REGISTER_FEATURE_PHASED("m2memory-arena", wxl::features::kM2Memory, ReserveM2Arena,
                            ::wxl::hook::Phase::Boot)
WXL_REGISTER_FEATURE("m2memory", wxl::features::kM2Memory, InstallM2Memory)
