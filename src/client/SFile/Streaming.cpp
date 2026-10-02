// Async streaming detour: reentrant-drain serialization.
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

#include "common/Log.hpp"
#include "config.hpp"
#include "engine/hook/Hook.hpp"
#include "engine/hook/Registry.hpp"

#include "offsets/engine/Gx.hpp"
#include "offsets/game/World.hpp"

#include <windows.h>

#include "wxl/Common.h"

#include <cstdint>
#include <cstring>

namespace
{
    namespace wld   = wxl::offsets::game::world;
    namespace gxoff = wxl::offsets::engine::gx;

    wld::AsyncServiceQueuesFn g_origAsyncDrain = nullptr;

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
        constexpr uint32_t kWaitGuard     = 0x00B4A26C; // the client's async-wait reentrancy counter

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

    wld::AsyncFileReadWaitFn g_origAsyncFileReadWait = nullptr;

    /**
     * @brief Recovers kWaitGuard from a native early-return path that leaks it.
     *
     * AsyncFileReadWait asserts kWaitGuard is 0 on entry (fatal app-terminate otherwise), increments
     * it, then decrements it once before every NORMAL return. One path skips that decrement: if the
     * target object is already marked handled by the time the critical section is taken, the
     * function releases the lock and returns immediately, never reaching the decrement at the
     * bottom. That leaves the guard permanently incremented -- the very next call to this function
     * from ANYWHERE in the client (it has dozens of call sites) hits the entry assert and
     * fatal-terminates the process. Confirmed via direct disassembly of the native function; no
     * caller-side argument avoids this, the early-return is unconditional once that flag is set.
     * Rare under light/occasional calling, much easier to hit for any caller invoking this function
     * at high frequency (found via the existing diagnostic logging below, once one such caller
     * started doing exactly that).
     *
     * Fix: read the guard's own value immediately before and after the real call. A leak shows up
     * as an exact +1 net change (the increment ran, the matching decrement didn't) -- restore it to
     * whatever it was before this call, nothing more. Keying off the counter's own observed delta,
     * rather than trying to predict the leak from the object's state beforehand, means this can
     * never remove more than what THIS call itself left behind, regardless of what any other caller
     * or thread does to the same guard concurrently.
     */
    void __cdecl hkAsyncFileReadWait(void* obj)
    {
        const uint32_t before = adrain::Rd(adrain::kWaitGuard);
        g_origAsyncFileReadWait(obj);
        const uint32_t after = adrain::Rd(adrain::kWaitGuard);
        if (after == before + 1)
        {
            adrain::Wr(adrain::kWaitGuard, before);
            WLOG_WARN("AsyncFileReadWait: recovered a leaked reentrancy guard (obj=%p, caller=%p) -- "
                      "native early-return path skipped its own decrement", obj, WXL_RETURN_ADDRESS());
        }
    }

    /**
     * @brief Normal-phase install: reentrant-drain serialization.
     */
    bool InstallStreaming()
    {
        wxl::hook::Install("AsyncDrain", wld::kAsyncServiceQueues,
                           &hkAsyncDrain, &g_origAsyncDrain);
        wxl::hook::Install("AsyncFileReadWaitGuard", wld::kAsyncFileReadWait,
                           &hkAsyncFileReadWait, &g_origAsyncFileReadWait);
        return true;
    }
}

WXL_REGISTER_FEATURE("streaming", true, InstallStreaming)
