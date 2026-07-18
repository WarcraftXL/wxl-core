// Async streaming detours: extra disk-queue workers, reentrant-drain serialization, ADT chunk build.
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
#include "engine/events/Event.hpp"

#include "common/Log.hpp"
#include "offsets/engine/Gx.hpp"
#include "offsets/game/ADT.hpp"
#include "offsets/game/World.hpp"

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace
{
    namespace ev    = wxl::events;
    namespace adt   = wxl::offsets::game::adt;
    namespace wld   = wxl::offsets::game::world;
    namespace gxoff = wxl::offsets::engine::gx;

    wld::AsyncServiceQueuesFn      g_origAsyncDrain          = nullptr;
    wld::AsyncFileReadInitializeFn g_origAsyncFileReadInit   = nullptr;
    wld::AsyncFileReadObjectFn     g_origAsyncFileReadObject = nullptr; // captured, never called: hkAsyncFileReadObject fully replaces the original
    adt::Map_ChunkBuildFn          g_origChunkBuild          = nullptr;
    adt::ChunkDestroyFn            g_origChunkDestroy        = nullptr;

    // Per-thread async-drain recursion depth. A texture build force-waits nested reads, which re-enter the
    // completion drain; a nested pump running unrelated completions frees / rewrites a buffer the outer
    // build still uploads from (the 0x40cb6a use-after-free).
    thread_local int g_drainDepth = 0;

    // Serialize the reentrant drain. The completed-read queue is an intrusive doubly-linked list: each node
    // holds its link at node+0x28 {next, tagged-prev}; the head (a node base) is at the completed-head global.
    // Addresses + arithmetic verified against the drain's own head-unlink. The lock is a recursive
    // critical section taken by ECX.
    namespace adrain
    {
        constexpr uint32_t kLockEnter     = 0x00774640; // critical-section enter, ecx = lock
        constexpr uint32_t kLockLeave     = 0x00774650; // critical-section leave, ecx = lock
        constexpr uint32_t kAsyncLock     = 0x00B4A240; // the recursive queue lock
        constexpr uint32_t kCompletedHead = 0x00AC3474; // first completed node (sentinel/empty if &1 or 0)
        constexpr uint32_t kAwaitedObj    = 0x00B4A204; // node a force-wait blocks on (0 = none)
        constexpr uint32_t kPendingCount  = 0x00B4A1F8; // outstanding-completion counter
        constexpr uint32_t kLinkOffset    = 0x28;       // node -> link byte offset

        inline uint32_t Rd(uint32_t a)             { return *reinterpret_cast<uint32_t*>(a); }
        inline void     Wr(uint32_t a, uint32_t v) { *reinterpret_cast<uint32_t*>(a) = v; }
        inline uint8_t  RdB(uint32_t a)            { return *reinterpret_cast<uint8_t*>(a); }
        inline void     WrB(uint32_t a, uint8_t v) { *reinterpret_cast<uint8_t*>(a) = v; }
        inline void Lock()   { reinterpret_cast<void(__thiscall*)(uint32_t)>(kLockEnter)(kAsyncLock); }
        inline void Unlock() { reinterpret_cast<void(__thiscall*)(uint32_t)>(kLockLeave)(kAsyncLock); }

        // Detach one node from the completed list (the engine's own head-unlink arithmetic, generalised).
        void Unlink(uint32_t node)
        {
            const uint32_t linkNext = node + 0x28;
            const uint32_t next = Rd(linkNext);
            if (next != 0)
            {
                const uint32_t prev = Rd(node + 0x2c);
                const uint32_t prevSlot = ((prev & 1u) == 0u && prev != 0u)
                                              ? linkNext + (prev - Rd(next + 4))
                                              : (prev & 0xFFFFFFFEu);
                Wr(prevSlot, next);
                Wr(next + 4, Rd(node + 0x2c));
                Wr(linkNext, 0);
                Wr(node + 0x2c, 0);
            }
            else
            {
                const uint32_t prev = Rd(node + 0x2c);
                if ((prev & 1u) != 0u || prev == 0u)
                    Wr(prev & 0xFFFFFFFEu, 0);
                Wr(node + 0x2c, 0);
            }
        }

        // True if target is currently enqueued in the completed list.
        bool Enqueued(uint32_t target)
        {
            uint32_t node = Rd(kCompletedHead);
            if ((node & 1u) != 0u || node == 0u) return false;
            for (;;)
            {
                if (node == target) return true;
                const uint32_t next = Rd(node + 0x28);
                if (next == 0) return false;
                node = next - kLinkOffset;
            }
        }

        // The mip-source singleton an upload reads while we run a nested completion. kMipTablePtr is a
        // pointer whose buffer holds the per-mip source pointers (read as ((u32*)ptr)[mip]); kMipTableValid
        // gates that read. The awaited completion is itself a texture build that refills both with its own
        // aliases and frees its IO buffer, so the outer build whose GxTexUpdate force-waited us would resume
        // reading a clobbered, freed table (0x40cb6a). Snapshot the outer view, run the nested build, put it
        // back: the outer copy then reads its own pointers into its own still-live buffer. The real mip count
        // is <= 13; 16 dwords is a safe upper bound, and the table buffer is the 1024-DXT scratch so the
        // fixed-size copy is always in bounds.
        struct MipTableSnapshot
        {
            uint32_t ptr;
            uint32_t valid;
            uint32_t table[16];
            bool     hasTable;
        };

        inline MipTableSnapshot SnapshotMipTable()
        {
            MipTableSnapshot s{};
            s.ptr      = Rd(gxoff::kMipTablePtr);
            s.valid    = Rd(gxoff::kMipTableValid);
            s.hasTable = (s.ptr != 0);
            if (s.hasTable)
                std::memcpy(s.table, reinterpret_cast<const void*>(s.ptr), sizeof(s.table));
            return s;
        }

        inline void RestoreMipTable(const MipTableSnapshot& s)
        {
            Wr(gxoff::kMipTablePtr, s.ptr);
            if (s.hasTable)
                std::memcpy(reinterpret_cast<void*>(s.ptr), s.table, sizeof(s.table));
            Wr(gxoff::kMipTableValid, s.valid);
        }

        // Process ONLY the awaited node; leave every other completion queued for the outer pump.
        int DrainAwaitedOnly()
        {
            Lock();
            const uint32_t target = Rd(kAwaitedObj);
            if (target == 0) { Unlock(); return 1; }
            if (RdB(target + 0x21) != 0) // already serviced this turn
            {
                if (Rd(kAwaitedObj) == target) Wr(kAwaitedObj, 0);
                Unlock();
                return 1;
            }
            if (!Enqueued(target)) { Unlock(); return 1; } // armed but not yet delivered by the worker
            Unlink(target);
            if (Rd(kAwaitedObj) == target) Wr(kAwaitedObj, 0);
            WrB(target + 0x21, 1);
            Unlock(); // the engine releases the lock before every completion call

            // Save the outer build's mip-source view across the nested build this completion runs, then
            // restore it so the force-waiting outer GxTexUpdate resumes reading its own live aliases.
            const MipTableSnapshot snap = SnapshotMipTable();
            reinterpret_cast<void(__cdecl*)(uint32_t)>(Rd(target + 0x10))(Rd(target + 0x0c));
            RestoreMipTable(snap);

            Wr(kPendingCount, Rd(kPendingCount) - 1);
            return 1;
        }
    }

    /**
     * @brief Detours the async-queue drain to serialize reentrant pumps.
     *
     * Depth 0 runs the full engine drain. A reentrant pump (a build force-waiting a nested read) processes
     * only the node that wait is blocked on and leaves the rest, so no nested completion frees or rewrites
     * a buffer the outer build is still uploading from.
     */
    int __cdecl hkAsyncDrain(int a, int b)
    {
        if (g_drainDepth > 0)
            return adrain::DrainAwaitedOnly();
        ++g_drainDepth;
        const int r = g_origAsyncDrain(a, b);
        --g_drainDepth;
        return r;
    }

    /**
     * @brief Detours the disk-queue init to add 2 more worker threads alongside the native one.
     *
     * Runs the original body unmodified first (creates queue slot 0, "Disk Queue", exactly as shipped),
     * then extends slots 1/2 with the same native helpers the original loop itself calls when streaming
     * mode is on -- so the resulting AsyncQueue/AsyncThread objects are byte-identical in shape and
     * self-register into the generic tracking lists the native teardown already walks. Does not touch
     * the streaming-mode flag or anything else the original touches. On its own this creates 2 idle
     * worker threads; AsyncFileReadObject's enqueue routing (a separate detour) must also change for
     * either thread to ever receive work.
     * @param maxPerSecond  native param, forwarded unmodified.
     * @param pumpBudgetMs  native param, forwarded unmodified.
     */
    void __cdecl hkAsyncFileReadInitialize(uint32_t maxPerSecond, uint32_t pumpBudgetMs)
    {
        g_origAsyncFileReadInit(maxPerSecond, pumpBudgetMs);

        static const char* const kExtraNames[] = { "WXL Disk Queue 2", "WXL Disk Queue 3" };
        auto* slots = reinterpret_cast<void**>(wld::kAsyncQueueSlots);
        auto alloc = reinterpret_cast<wld::AsyncQueueAllocFn>(wld::kAsyncQueueAlloc);
        auto wrap  = reinterpret_cast<wld::AsyncThreadWrapFn>(wld::kAsyncThreadWrap);
        for (int i = 1; i <= 2; ++i)
        {
            void* queue = alloc();
            if (!queue) { WLOG_WARN("AsyncFileReadInitialize: extra queue %d alloc failed", i); continue; }
            slots[i] = queue;
            wrap(queue, kExtraNames[i - 1]);
        }
        WLOG_INFO("AsyncFileReadInitialize: extended to 3 disk-queue workers");
    }

    // Native async-object layout (0x30 bytes total). Only the fields the enqueue detour touches are
    // named; the rest stay opaque padding.
#pragma pack(push, 1)
    struct AsyncObjectView
    {
        uint8_t  _pad00[0x18]; // handle/buffer/len/owner/completion, untouched here
        uint32_t queue;        // +0x18
        uint32_t timestamp;    // +0x1c
        uint8_t  priority;     // +0x20
        uint8_t  _pad21[3];    // +0x21..0x23: serviced/queue-state/in-progress flags, untouched here
        uint8_t  netFlag;      // +0x24, untouched here (only ever set on the streaming-only routing path)
        uint8_t  rearm;        // +0x25
    };
    // Native AsyncQueue layout: only the "list B" selector flag is touched here.
    struct AsyncQueueView
    {
        uint8_t  _pad00[0x20];
        uint32_t secondListFlag; // +0x20
    };
#pragma pack(pop)
    static_assert(sizeof(AsyncObjectView) <= 0x30, "AsyncObjectView must fit the native 0x30-byte object");
    static_assert(offsetof(AsyncObjectView, queue)     == 0x18, "AsyncObjectView.queue");
    static_assert(offsetof(AsyncObjectView, timestamp) == 0x1c, "AsyncObjectView.timestamp");
    static_assert(offsetof(AsyncObjectView, priority)  == 0x20, "AsyncObjectView.priority");
    static_assert(offsetof(AsyncObjectView, netFlag)   == 0x24, "AsyncObjectView.netFlag");
    static_assert(offsetof(AsyncObjectView, rearm)     == 0x25, "AsyncObjectView.rearm");
    static_assert(offsetof(AsyncQueueView, secondListFlag) == 0x20, "AsyncQueueView.secondListFlag");

    /**
     * @brief Detours the disk-queue enqueue to round-robin across every live worker queue.
     *
     * Native code always selects queue slot 0 outside streaming mode regardless of how many worker
     * threads exist, which left AsyncFileReadInitialize's 2 extra threads permanently idle. Reimplements
     * only the queue-selection step; the lock, force-wait fast path and insert dispatch call the exact
     * same native subroutines the original uses, byte-for-byte, so behaviour is unchanged apart from
     * which queue an object lands in. A trailing streaming-mode-gated no-op call in the original is not
     * reachable outside streaming mode, so it is omitted.
     * @param obj               async object being enqueued.
     * @param highPriorityFlag  native param, forwarded unmodified to the insert call.
     */
    void __cdecl hkAsyncFileReadObject(void* obj, uint32_t highPriorityFlag)
    {
        auto* o = static_cast<AsyncObjectView*>(obj);
        auto* slots = reinterpret_cast<void* const*>(wld::kAsyncQueueSlots);

        static std::atomic<uint32_t> s_rr{ 0 };
        const uint32_t live = slots[1] ? (slots[2] ? 3u : 2u) : 1u;
        void* queue = slots[s_rr.fetch_add(1, std::memory_order_relaxed) % live];
        auto* q = static_cast<AsyncQueueView*>(queue);

        adrain::Lock();
        const bool forceWait = (reinterpret_cast<uint32_t>(obj) == adrain::Rd(adrain::kAwaitedObj));
        o->queue = reinterpret_cast<uint32_t>(queue);

        if (forceWait)
        {
            o->priority = (o->priority > 0x7f) ? 0x80 : 0;
            o->timestamp = *reinterpret_cast<uint32_t*>(
                *reinterpret_cast<uint8_t**>(gxoff::kGxDevicePtr) + 0xf68);
            reinterpret_cast<wld::TSListLinkToHeadFn>(wld::kTSListLinkToHead)(
                reinterpret_cast<uint8_t*>(queue) + 0x8, obj);
            o->rearm = 0;
        }
        else if (q->secondListFlag == 0)
        {
            reinterpret_cast<wld::AsyncFileReadLinkObjectFn>(wld::kAsyncFileReadLinkObject)(obj, highPriorityFlag);
        }
        else
        {
            reinterpret_cast<wld::AsyncFileReadLinkObjectFn>(wld::kAsyncFileReadLinkObjectAlt)(obj, highPriorityFlag);
        }
        adrain::Unlock();
    }

    /**
     * @brief Detours map-chunk build, emitting OnAdtChunkBuild with the chunk and its layer count.
     *
     * Runs after the native build populates the sub-chunk pointers and layer units, so the
     * texture-layer count is readable.
     * @param chunk    map chunk.
     * @param edx      unused register slot for the thiscall convention.
     * @param rawMcnk  raw chunk data.
     * @param param2   native build parameter.
     */
    void __fastcall hkChunkBuild(void* chunk, void* edx, void* rawMcnk, int param2)
    {
        g_origChunkBuild(chunk, edx, rawMcnk, param2);
        uint32_t layers = 0;
        void* header = static_cast<adt::MapChunk*>(chunk)->mcnkHeader;
        if (header) layers = static_cast<adt::McnkHeader*>(header)->nLayers;
        ev::AdtChunkArgs a{ chunk, layers };
        ev::Emit(ev::Event::OnAdtChunkBuild, &a);
    }

    /**
     * @brief Detours map-chunk teardown to cancel its in-flight async read before the buffer is freed.
     *
     * The chunk's completed-read callback parses the chunk's IO buffer (+0x80). If a teardown frees that
     * buffer while the read is still queued, the later completion parses freed memory and faults
     * (0x7d6f05). Retiring the async object (+0x70) here unlinks the pending completion first.
     * @param chunk  CMapChunk being destroyed (ECX).
     */
    void __fastcall hkChunkDestroy(void* chunk)
    {
        if (chunk)
        {
            auto* slot = reinterpret_cast<void**>(static_cast<uint8_t*>(chunk) + adt::kOffChunkAsyncObj);
            if (*slot)
            {
                reinterpret_cast<wld::AsyncDestroyFn>(wld::kAsyncDestroy)(*slot);
                *slot = nullptr;
            }
        }
        g_origChunkDestroy(chunk);
    }

    /**
     * @brief Boot-phase install: the disk-queue worker-count extension and its enqueue routing.
     *
     * The client creates its disk-queue worker thread very early in boot, well before the render device
     * exists -- so these two detours must be live before the client's own startup reaches them, which is
     * why they install (and get enabled) on the loader thread rather than at the Normal phase every other
     * streaming detour uses. Only extends an already-initialized subsystem after the real call runs
     * (call-original-then-extend); does not touch file-content serving, so it carries none of the
     * heap-corruption risk that keeps the content-serving hooks deferred.
     */
    bool InstallStreamingEarly()
    {
        wxl::hook::Install("AsyncFileReadInitialize", wld::kAsyncFileReadInitialize,
                           &hkAsyncFileReadInitialize, &g_origAsyncFileReadInit);
        wxl::hook::Install("AsyncFileReadObject", wld::kAsyncFileReadObject,
                           &hkAsyncFileReadObject, &g_origAsyncFileReadObject);
        return true;
    }

    /**
     * @brief Normal-phase install: reentrant-drain serialization and ADT chunk-build event.
     */
    bool InstallStreaming()
    {
        wxl::hook::Install("AsyncDrain", wld::kAsyncServiceQueues,
                           &hkAsyncDrain, &g_origAsyncDrain);
        wxl::hook::Install("ChunkBuild", adt::kChunkBuild,
                           &hkChunkBuild, &g_origChunkBuild);
        // ChunkDestroy (ADT cancel-on-teardown) temporarily disabled: it correlates with a render-path
        // null-deref (0x7c846c) and the cancel timing relative to a sibling free is unconfirmed. Re-enable
        // once that ordering is confirmed as teardown rather than a sibling free.
        (void)&hkChunkDestroy; (void)&g_origChunkDestroy;
        return true;
    }
}

WXL_REGISTER_FEATURE_PHASED("streaming-early", wxl::features::kStreaming, InstallStreamingEarly,
                            ::wxl::hook::Phase::Boot)
WXL_REGISTER_FEATURE("streaming", wxl::features::kStreaming, InstallStreaming)
