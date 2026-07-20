// Game-logic detours: publish OnModelLoad and other non-render events.
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

#include "runtime/GameHooks.hpp"
#include "runtime/AssetProfile.hpp"
#include "runtime/LuaBindings.hpp"

#include "common/Config.hpp"
#include "core/Hook.hpp"
#include "core/Logger.hpp"
#include "core/Mem.hpp"
#include "events/Event.hpp"
#include "game/m2/M2.hpp"
#include "offsets/engine/Frame.hpp"
#include "offsets/engine/Gx.hpp"
#include "offsets/engine/Sound.hpp"
#include "offsets/game/ADT.hpp"
#include "offsets/game/Doodad.hpp"
#include "offsets/game/M2.hpp"
#include "offsets/game/Unit.hpp"
#include "offsets/game/WMO.hpp"
#include "offsets/game/World.hpp"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
    namespace ev    = wxl::events;
    namespace m2    = wxl::offsets::game::m2;
    namespace gxoff = wxl::offsets::engine::gx;
    namespace adt   = wxl::offsets::game::adt;
    namespace dd    = wxl::offsets::game::doodad;
    namespace wmo   = wxl::offsets::game::wmo;
    namespace wld   = wxl::offsets::game::world;
    namespace frame = wxl::offsets::engine::frame;
    namespace unit  = wxl::offsets::game::unit;
    namespace snd   = wxl::offsets::engine::sound;
    namespace aprof = wxl::runtime::assetprof;

    m2::M2_InitFn              g_origM2Init            = nullptr;
    m2::M2_FinalizeSkinFn      g_origFinalizeSkin      = nullptr;
    m2::M2_BuildBatchMaterialFn g_origBuildBatchMaterial = nullptr;
    m2::M2_BufferAllocFn       g_origM2BufferAlloc     = nullptr;
    m2::M2_BufferFreeFn        g_origM2BufferFree      = nullptr;
    m2::M2_SetupBatchAlphaFn   g_origSetupAlpha    = nullptr;
    m2::M2_SortOpaqueGeoBatchesFn g_origSortOpaqueGeoBatches = nullptr;
    m2::M2_SlotDispatchFn      g_origSlotDispatch  = nullptr;
    m2::M2_SlotClearFn         g_origSlotClear     = nullptr;
    m2::M2_PerFrameUpdateFn    g_origM2PerFrame    = nullptr;
    m2::M2_BuildBonePaletteFn  g_origBuildBonePalette = nullptr;
    m2::M2_RenderBatchShadowMapFn g_origRenderBatchShadowMap = nullptr;
    m2::M2_SceneTriangleHitTestFn g_origSceneTriangleHitTest = nullptr;
    dd::SpawnFromMDDFFn        g_origDoodadSpawn  = nullptr;
    wmo::Wmo_SpawnFromModfFn   g_origWmoSpawn     = nullptr;
    gxoff::TextureUpdateFn       g_origTexUpdate    = nullptr;
    gxoff::TextureCreateFn       g_origTexCreate    = nullptr;
    wld::AsyncServiceQueuesFn    g_origAsyncDrain   = nullptr;
    wld::AsyncFileReadInitializeFn g_origAsyncFileReadInit = nullptr;
    wld::AsyncFileReadObjectFn   g_origAsyncFileReadObject = nullptr; // captured, never called: hkAsyncFileReadObject fully replaces the original
    adt::Map_ChunkBuildFn      g_origChunkBuild   = nullptr;
    adt::ChunkDestroyFn        g_origChunkDestroy = nullptr;
    wmo::Wmo_RootCompleteFn    g_origWmoRoot      = nullptr;
    wmo::WmoGroup_ParseFn      g_origWmoGroup     = nullptr;
    wld::World_EnterFn         g_origWorldEnter   = nullptr;
    frame::FramePumpFn         g_origFramePump    = nullptr;
    unit::ObjectMsgHandlerFn   g_origObjUpdate    = nullptr;
    unit::ObjectMsgHandlerFn   g_origObjDestroy   = nullptr;
    unit::TargetSetFn          g_origTargetSet    = nullptr;
    unit::UnitFieldSetWriteFn  g_origUnitFieldSetWrite = nullptr;
    snd::PlaySoundFn           g_origPlaySound    = nullptr;
    snd::PlaySoundKitFn        g_origPlaySoundKit = nullptr;
    std::atomic<uint32_t>      g_sceneHitTestFaults{ 0 };
    std::atomic<uint32_t>      g_textureUpdateFaults{ 0 };
    std::atomic<uint32_t>      g_opaqueSortFaults{ 0 };
    std::atomic<uint32_t>      g_shadowBoneOverflowSkips{ 0 };

    constexpr uint32_t kDefaultVirtualM2AllocThreshold = 1u * 1024u * 1024u;
    // Reserve only what the observed city workload actually needs. A 256 MB reservation stranded roughly
    // 190 MB of scarce 32-bit VA while CM2Model later failed to find a separate 15 MB contiguous block.
    constexpr uint32_t kDefaultM2ArenaSizeMb = 128u;

    /**
     * @brief Contains a stale M2 collision-buffer read after disconnect/reconnect world teardown.
     *
     * Returning the caller's current hit is the native loop's no-new-triangle result. Keeping the guard at
     * this leaf lets CM2Scene's outer geometry/collision code finish its cleanup and matrix restoration.
     */
    int __fastcall hkSceneTriangleHitTest(
        void* scratch, void* /*edx*/, uint16_t* indexBegin, uint16_t* indexEnd, int vertexBase,
        float* point, int mode, int candidate, float* bestDepth, int currentHit)
    {
        __try
        {
            return g_origSceneTriangleHitTest(
                scratch, indexBegin, indexEnd, vertexBase, point, mode, candidate, bestDepth, currentHit);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            const uint32_t faults = g_sceneHitTestFaults.fetch_add(1, std::memory_order_relaxed) + 1;
            if (faults == 1 || (faults & (faults - 1)) == 0)
                WLOG_WARN("M2 scene hit-test skipped stale collision data (faults=%u)", faults);
            return currentHit;
        }
    }

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
        {
            std::lock_guard<std::mutex> lock(g_virtualM2AllocMutex);
            auto it = g_virtualM2Allocs.find(ptr);
            if (it != g_virtualM2Allocs.end())
            {
                alloc = it->second;
                g_virtualM2Allocs.erase(it);
                ours = true;
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
     * @brief Detours model init, emitting OnModelLoadPre at entry and OnModelLoad after parsing.
     * @param model  runtime model receiving the parsed file (raw bytes at model+0x150, size at +0x16c).
     * @return the original model-init result.
     */
    int __fastcall hkM2Init(void* model)
    {
        const uint64_t preStarted = aprof::Now();
        ev::ModelLoadArgs pre{ model };
        ev::Emit(ev::Event::OnModelLoadPre, &pre);
        if (preStarted) aprof::Record(aprof::Phase::M2Pre, aprof::Now() - preStarted);

        const uint64_t nativeStarted = aprof::Now();
        const int r = g_origM2Init(model);
        if (nativeStarted) aprof::Record(aprof::Phase::M2Native, aprof::Now() - nativeStarted);

        const uint64_t postStarted = aprof::Now();
        ev::ModelLoadArgs a{ model };
        ev::Emit(ev::Event::OnModelLoad, &a);
        if (postStarted) aprof::Record(aprof::Phase::M2Post, aprof::Now() - postStarted);
        return r;
    }

    bool ProbeEffectObject(void* effect) noexcept
    {
        if (!effect) return true;
        __try
        {
            volatile uint8_t head = *static_cast<uint8_t*>(effect);
            volatile uint32_t vertexTable = *reinterpret_cast<uint32_t*>(
                static_cast<uint8_t*>(effect) + 0x2C);
            volatile uint32_t pixelTable = *reinterpret_cast<uint32_t*>(
                static_cast<uint8_t*>(effect) + 0x194);
            (void)head;
            (void)vertexTable;
            (void)pixelTable;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    /**
     * @brief Detours skin finalize, emitting OnM2SkinFinalize before the native sizing runs.
     *
     * The skin profile is attached, pointer-fixed and its header live before the native finalize
     * sizes its parallel batch blocks from skin->batchCount, so a subscriber can rebuild a
     * material/texunit contract a modern skin omits.
     * @param model  model whose skin is being finalized.
     */
    void __fastcall hkFinalizeSkin(void* model)
    {
        ev::M2SkinFinalizeArgs a{ model };
        ev::Emit(ev::Event::OnM2SkinFinalize, &a);
        g_origFinalizeSkin(model);

        // Native finalize stores one optional CShaderEffect pointer per skin batch at model+0x188.
        // Diagnose and clear values that are already invalid here; the sorter guard below covers keys
        // that become stale later (for example across an effect-manager/device lifecycle transition).
        uint32_t invalid = 0;
        uint32_t firstIndex = 0;
        void* firstEffect = nullptr;
        char path[128]{};
        __try
        {
            auto* bytes = static_cast<uint8_t*>(model);
            auto* skin = *reinterpret_cast<wxl::game::m2::M2SkinProfile**>(bytes + m2::kOffModelSkin);
            auto** effects = *reinterpret_cast<void***>(bytes + m2::kOffModelSubMeshCopy);
            if (skin && effects)
            {
                const uint32_t count = std::min<uint32_t>(skin->batchCount, 0x4000u);
                for (uint32_t i = 0; i < count; ++i)
                {
                    void* effect = effects[i];
                    if (effect && !ProbeEffectObject(effect))
                    {
                        if (invalid == 0) { firstIndex = i; firstEffect = effect; }
                        effects[i] = nullptr;
                        ++invalid;
                    }
                }
            }

            const char* source = reinterpret_cast<const char*>(bytes + m2::kOffModelPathStem);
            size_t i = 0;
            for (; i + 1 < sizeof path && source[i]; ++i) path[i] = source[i];
            path[i] = '\0';
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            path[0] = '\0';
        }
        if (invalid)
            WLOG_WARN("M2 finalize: cleared %u invalid effect key(s), first batch=%u effect=%p model='%s'",
                      invalid, firstIndex, firstEffect, path[0] ? path : "(unreadable)");
    }

    /**
     * @brief Probes the optional shader-effect sort key carried by one 0x44-byte CM2 scene element.
     *
     * CM2Scene::SortOpaqueGeoBatches reads two effect-owned arrays through element+0x30 using the
     * vertex/pixel shader indices at +0x34/+0x38. The effect pointer is not needed by DrawBatch itself;
     * it only refines ordering. A stale key can therefore be cleared without dropping the geometry.
     */
    bool ProbeOpaqueEffectKey(void* rawEntry, void** effectOut, int32_t* vertexIndexOut,
                              int32_t* pixelIndexOut) noexcept
    {
        if (effectOut) *effectOut = nullptr;
        if (vertexIndexOut) *vertexIndexOut = -1;
        if (pixelIndexOut) *pixelIndexOut = -1;
        if (!rawEntry) return false;

        __try
        {
            auto* entry = static_cast<uint8_t*>(rawEntry);
            void* effect = *reinterpret_cast<void**>(entry + 0x30);
            const int32_t vertexIndex = *reinterpret_cast<int32_t*>(entry + 0x34);
            const int32_t pixelIndex = *reinterpret_cast<int32_t*>(entry + 0x38);
            if (effectOut) *effectOut = effect;
            if (vertexIndexOut) *vertexIndexOut = vertexIndex;
            if (pixelIndexOut) *pixelIndexOut = pixelIndex;
            if (!effect) return true;

            // ComputeElementShaders produces small non-negative table indices. Rejecting absurd values
            // also prevents signed index wrap before touching the effect-owned arrays.
            if (vertexIndex < 0 || pixelIndex < 0 || vertexIndex > 0x10000 || pixelIndex > 0x10000)
                return false;

            volatile uint32_t vertexKey = *reinterpret_cast<uint32_t*>(
                static_cast<uint8_t*>(effect) + 0x2C + static_cast<uint32_t>(vertexIndex) * 4u);
            volatile uint32_t pixelKey = *reinterpret_cast<uint32_t*>(
                static_cast<uint8_t*>(effect) + 0x194 + static_cast<uint32_t>(pixelIndex) * 4u);
            (void)vertexKey;
            (void)pixelKey;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void ClearOpaqueEffectKey(void* rawEntry) noexcept
    {
        if (!rawEntry) return;
        __try
        {
            *reinterpret_cast<void**>(static_cast<uint8_t*>(rawEntry) + 0x30) = nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    int PointerOrder(const void* lhs, const void* rhs) noexcept
    {
        const uintptr_t a = reinterpret_cast<uintptr_t>(lhs);
        const uintptr_t b = reinterpret_cast<uintptr_t>(rhs);
        return a < b ? -1 : (a > b ? 1 : 0);
    }

    int __cdecl hkSortOpaqueGeoBatches(void* lhs, void* rhs)
    {
        void* lhsEffect = nullptr;
        void* rhsEffect = nullptr;
        int32_t lhsVs = -1, lhsPs = -1, rhsVs = -1, rhsPs = -1;
        const bool lhsOk = ProbeOpaqueEffectKey(lhs, &lhsEffect, &lhsVs, &lhsPs);
        const bool rhsOk = ProbeOpaqueEffectKey(rhs, &rhsEffect, &rhsVs, &rhsPs);

        if (!lhsOk) ClearOpaqueEffectKey(lhs);
        if (!rhsOk) ClearOpaqueEffectKey(rhs);
        if (!lhsOk || !rhsOk)
        {
            const uint32_t faults = g_opaqueSortFaults.fetch_add(1, std::memory_order_relaxed) + 1;
            if (faults == 1 || (faults & (faults - 1)) == 0)
                WLOG_WARN("M2 opaque-sort: cleared stale effect key (faults=%u lhsFx=%p lhsVS=%d lhsPS=%d rhsFx=%p rhsVS=%d rhsPS=%d)",
                          faults, lhsEffect, lhsVs, lhsPs, rhsEffect, rhsVs, rhsPs);
        }

        __try
        {
            return g_origSortOpaqueGeoBatches(lhs, rhs);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            const uint32_t faults = g_opaqueSortFaults.fetch_add(1, std::memory_order_relaxed) + 1;
            if (faults == 1 || (faults & (faults - 1)) == 0)
                WLOG_WARN("M2 opaque-sort: native comparator fault quarantined (faults=%u lhs=%p rhs=%p)",
                          faults, lhs, rhs);
            return PointerOrder(lhs, rhs);
        }
    }

    /**
     * @brief Guards the per-batch material-key builder against unimplemented shader types.
     *
     * kBuildBatchMaterial reads M2Batch::shaderId (batch+2) and uses bits 0-14 as a
     * switch index when bit 0x8000 is set. The switch only handles values 0-3; for higher values it
     * falls through with EBX=0 and crashes at 0x836D11 (mov cl, [eax] with eax=0).
     * Modern collection M2s can have shaderId values > 3; returning nullptr for those is safe because
     * kFinalizeSkin only stores the result in the model+0x188 array, which the IB build path does not
     * dereference directly.
     * @param model    model object (ECX thiscall this pointer).
     * @param edx      unused register slot (thiscall via fastcall trampoline).
     * @param batchPtr pointer to the M2Batch entry from skin->batches.
     * @return the material-key object, or nullptr for unimplemented shader types.
     */
    void* __fastcall hkBuildBatchMaterial(void* model, void* /*edx*/, void* batchPtr)
    {
        if (batchPtr)
        {
            uint16_t shaderId = *reinterpret_cast<uint16_t*>(
                static_cast<uint8_t*>(batchPtr) + 2);
            if ((shaderId & 0x8000u) && (shaderId & 0x7FFFu) > 3u)
                return nullptr;
        }
        return g_origBuildBatchMaterial(model, batchPtr);
    }

    /**
     * @brief Detours per-batch alpha/material setup, emitting OnM2SetupBatchAlpha with the model and blend.
     *
     * Runs after the native setter picks the alpha-test reference from the blend mode, so a
     * subscriber can re-push a different reference. The draw-context reads are guarded so a
     * malformed context never faults the render thread.
     * @param ctx  draw context.
     */
    void __fastcall hkSetupBatchAlpha(void* ctx)
    {
        g_origSetupAlpha(ctx);

        void*    model = nullptr;
        uint16_t blend = 0;
        __try
        {
            auto* dc   = static_cast<m2::DrawContext*>(ctx);
            void* inst = dc->instance;
            void* mat  = dc->material;
            if (inst) model = static_cast<m2::M2Instance*>(inst)->model;
            if (mat)  blend = static_cast<m2::Material*>(mat)->blend;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { model = nullptr; }

        if (model)
        {
            ev::M2SetupBatchAlphaArgs a{ model, blend };
            ev::Emit(ev::Event::OnM2SetupBatchAlpha, &a);
        }
    }

    /**
     * @brief Detours doodad spawn, emitting OnDoodadSpawn with the placement the native call built.
     * @param modelName   doodad model name.
     * @param mddf        placement record.
     * @param tileOrigin  tile origin for the placement.
     * @return the spawned doodad.
     */
    void* __cdecl hkDoodadSpawn(const char* modelName, void* mddf, void* tileOrigin)
    {
        void* doodad = g_origDoodadSpawn(modelName, mddf, tileOrigin);
        ev::DoodadSpawnArgs a{ doodad };
        ev::Emit(ev::Event::OnDoodadSpawn, &a);
        return doodad;
    }

    /** @brief Multiplies the upper-left 3x3 rows of a 4x4 row-major matrix by a uniform factor. */
    void ScaleMatrixRows3x3(float* m, float s)
    {
        m[0] *= s; m[1] *= s; m[2]  *= s;
        m[4] *= s; m[5] *= s; m[6]  *= s;
        m[8] *= s; m[9] *= s; m[10] *= s;
    }

    /**
     * @brief Reports a freshly spawned WMO's live doodad-set selection against its loaded MODS.
     *
     * Catches the in-game "all doodad sets render at once" case by reading the post-down-convert MODS the
     * Client actually loaded (not the on-disk file) plus the instance's selected/extra sets. Logs only a
     * suspicious shape: an extra set populated, a selected index out of range, or a set 0 whose MODD range
     * swallows the other content sets (every doodad then resolves to set 0, which renders unconditionally).
     * A correctly selected WMO stays silent.
     * @param inst  freshly spawned WMO instance.
     */
    void DiagDoodadSets(void* inst)
    {
        auto* root = *reinterpret_cast<void**>(static_cast<uint8_t*>(inst) + wmo::kOffInstanceRoot);
        if (!root)
            return;
        const uint32_t nSets = *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(root) + wmo::kOffRootDoodadSets);
        if (nSets < 2)
            return; // a single-set WMO cannot show "extra" sets
        const uint8_t* mods = *reinterpret_cast<uint8_t**>(static_cast<uint8_t*>(root) + wmo::kOffRootMods);
        if (!mods)
            return;
        const uint32_t nDefs = *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(root) + wmo::kOffRootDoodadDefs);
        const uint32_t sel   = *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(inst) + wmo::kOffInstanceDoodadSet);
        const uint16_t* extra = reinterpret_cast<uint16_t*>(static_cast<uint8_t*>(inst) + wmo::kOffInstanceExtraSets);
        const char* name = reinterpret_cast<const char*>(static_cast<uint8_t*>(root) + wmo::kOffNameInline);
        const uint32_t s0count = *reinterpret_cast<const uint32_t*>(mods + wmo::kOffModsCount);

        const bool greedy0  = nDefs && s0count + 1 >= nDefs;    // set 0 covers (almost) every def
        const bool selOob   = sel >= nSets;                     // selected index out of range
        const bool hasExtra = extra[0] || extra[1] || extra[2]; // extra sets populated
        if (!greedy0 && !selOob && !hasExtra)
            return; // selection resolves to {set0, sel} only -> correct, stay silent

        WLOG_INFO("wmo-doodad-diag: %.96s nSets=%u nDefs=%u sel=%u extra={%u,%u,%u} set0count=%u%s%s%s",
            name, nSets, nDefs, sel, extra[0], extra[1], extra[2], s0count,
            greedy0 ? " GREEDY-SET0" : "", selOob ? " SEL-OOB" : "", hasExtra ? " EXTRA-SETS" : "");
    }

    /**
     * @brief Detours the WMO instance spawn, applying the per-instance MODF scale the Client ignores.
     *
     * The Client builds the instance at scale 1.0 (MODF+0x3E is padding to it). After the native spawn,
     * the modern scale is folded into the render matrix (+0x70). The collision/portal copy (+0xB0) is a
     * transposed read-back the portal-visibility test reads as an inverse rotation; scaling it breaks that
     * test and culls interior groups (the WMO goes invisible), so it is left at 1.0 (collision stays at
     * native size). A dedup hit returns an already-scaled instance, so the scale is applied only to a
     * freshly built instance, recognised by its still-orthonormal basis (|row0| == 1); this is reload-safe
     * and needs no per-instance bookkeeping.
     * @param ctx         world context.
     * @param modf        MODF placement record.
     * @param tileOrigin  tile world origin.
     * @param dedup       non-zero to return an existing instance for a known uniqueId.
     * @return the spawned (or existing) instance.
     */
    void* __cdecl hkWmoSpawn(void* ctx, void* modf, const float* tileOrigin, int dedup)
    {
        void* inst = g_origWmoSpawn(ctx, modf, tileOrigin, dedup);
        if (!inst || !modf)
            return inst;

        DiagDoodadSets(inst);

        const uint16_t raw = *reinterpret_cast<uint16_t*>(static_cast<uint8_t*>(modf) + wmo::kOffModfScale);
        if (raw == 0 || raw == 1024)
            return inst; // native / unscaled: leave the instance byte-for-byte
        const float s = static_cast<float>(raw) / 1024.0f;

        float* render = reinterpret_cast<float*>(static_cast<uint8_t*>(inst) + wmo::kOffInstanceRenderMatrix);
        const float len2 = render[0] * render[0] + render[1] * render[1] + render[2] * render[2];
        if (len2 < 0.9999f || len2 > 1.0001f)
            return inst; // already scaled (a dedup hit returned an existing instance)

        ScaleMatrixRows3x3(render, s);
        return inst;
    }

    /**
     * @brief Detours texture upload, emitting OnTextureUpload before the device update.
     *
     * width=x2-x and height=y2-y cover both full-surface (x=y=0) and sub-rect uploads.
     *
     * The mip source the upload reads is a process-wide singleton (kMipTablePtr is a pointer whose
     * buffer holds the per-mip source pointers; kMipTableValid gates the read). A build fills it with
     * raw aliases into its transient IO buffer, then uploads. Two ways that singleton turns into an
     * access-violation use-after-free (0x40cb6a), both fixed here without ever clearing kMipTableValid
     * (which would route atlas icons through a source callback that has no self-heal and blank them):
     *  - a NESTED build run while this upload is mid-copy overwrites the table and frees its buffer; the
     *    async-drain serializer snapshots and restores the table around the nested build it runs
     *    (adrain::DrainAwaitedOnly), so the outer upload keeps reading its own live aliases.
     *  - a TRUNCATED mip chain (common in custom-map BLPs) only fills the low slots, so the dimension-
     *    driven upload would read a prior build's freed alias left in a high slot. Clearing the table
     *    after each upload leaves the next build's unfilled slots at 0, which the upload's source-not-null
     *    blit guard skips. Done after the original consumed the table, so the live upload is unaffected.
     * @param tex   texture being uploaded.
     * @param x     upload rect left.
     * @param y     upload rect top.
     * @param x2    upload rect right.
     * @param y2    upload rect bottom.
     * @param flag  native upload flag.
     */
    // Keep SEH in a POD-only leaf. TextureUpdate invokes the texture's completion callback before it
    // returns; a late font-atlas/cache callback can retain a row/tree pointer whose owner was rebuilt
    // during world entry. Letting that AV escape kills the client from TextureCallback (0x006C9F50).
    bool SafeTextureUpdate(void* tex, int x, int y, int x2, int y2, int flag) noexcept
    {
        __try
        {
            g_origTexUpdate(tex, x, y, x2, y2, flag);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void __cdecl hkTexUpdate(void* tex, int x, int y, int x2, int y2, int flag)
    {
        ev::TextureUploadArgs a{ tex, static_cast<uint32_t>(x2 - x), static_cast<uint32_t>(y2 - y) };
        ev::Emit(ev::Event::OnTextureUpload, &a);
        const uint64_t started = aprof::Now();
        const bool completed = SafeTextureUpdate(tex, x, y, x2, y2, flag);
        if (!completed)
        {
            const uint32_t faults = g_textureUpdateFaults.fetch_add(1, std::memory_order_relaxed) + 1;
            if (faults == 1 || (faults & (faults - 1)) == 0)
                WLOG_WARN("texture: skipped stale native upload callback (faults=%u tex=%p rect=%d,%d..%d,%d)",
                          faults, tex, x, y, x2, y2);
        }
        else if (started)
        {
            const uint64_t width = x2 > x ? static_cast<uint64_t>(x2 - x) : 0;
            const uint64_t height = y2 > y ? static_cast<uint64_t>(y2 - y) : 0;
            aprof::Record(aprof::Phase::TextureUpload, aprof::Now() - started, width * height);
        }
        if (auto* tbl = *reinterpret_cast<uint32_t**>(gxoff::kMipTablePtr))
            std::memset(tbl, 0, gxoff::kMipTableSlots * sizeof(uint32_t));
    }

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
     * @brief Detours the by-name texture create, emitting OnBlpLoad after the request resolves.
     *
     * Fires on every reference (returns the cached handle on a hit), so the event carries the requested
     * name and a subscriber can watch for one specific BLP. Logs each distinct name once, so the log lists
     * BLPs as they first load without flooding. The name set is mutex-guarded as the request can arrive
     * from a loader thread.
     * @param name    requested texture path (full virtual path).
     * @param flags   native load flags.
     * @param status  native status out-pointer.
     * @param flags2  native load flags.
     * @return the resolved texture handle (null on failure).
     */
    void* __cdecl hkTexCreate(const char* name, uint32_t flags, int* status, uint32_t flags2)
    {
        const uint64_t started = aprof::Now();
        void* handle = g_origTexCreate(name, flags, status, flags2);
        if (started) aprof::Record(aprof::Phase::TextureRequest, aprof::Now() - started);

        ev::BlpLoadArgs a{ name, handle };
        ev::Emit(ev::Event::OnBlpLoad, &a);

        return handle;
    }

    /**
     * @brief Detours the server object update-block handler, emitting OnObjectUpdate after the parse.
     *
     * One fire per update message (a batch of created/updated objects). Logs the first fire only.
     * @param ctx     handler context.
     * @param opcode  message opcode.
     * @param msg     message id.
     * @param packet  inbound message reader.
     * @return the native handler result.
     */
    int __cdecl hkObjUpdate(void* ctx, int opcode, int msg, void* packet)
    {
        const int r = g_origObjUpdate(ctx, opcode, msg, packet);

        ev::ObjectUpdateArgs a{ packet, opcode };
        ev::Emit(ev::Event::OnObjectUpdate, &a);

        static bool logged = false;
        if (!logged) { logged = true; WLOG_INFO("object: update stream active"); }
        return r;
    }

    /**
     * @brief Detours the object destroy handler, emitting OnObjectDestroy before the despawn.
     *
     * One fire per despawn, while the object is still resident. Logs the first fire only.
     * @param ctx     handler context.
     * @param opcode  message opcode.
     * @param msg     message id.
     * @param packet  inbound message reader (object GUID + on-death flag).
     * @return the native handler result.
     */
    int __cdecl hkObjDestroy(void* ctx, int opcode, int msg, void* packet)
    {
        ev::ObjectDestroyArgs a{ packet, opcode };
        ev::Emit(ev::Event::OnObjectDestroy, &a);

        static bool logged = false;
        if (!logged) { logged = true; WLOG_INFO("object: destroy hook active"); }
        return g_origObjDestroy(ctx, opcode, msg, packet);
    }

    /**
     * @brief Detours the target-set API, emitting OnTargetChanged after the new target is applied.
     * @param scriptState  script state the call ran on.
     * @return the native function result.
     */
    int __cdecl hkTargetSet(void* scriptState)
    {
        const int r = g_origTargetSet(scriptState);

        ev::TargetChangedArgs a{ scriptState };
        ev::Emit(ev::Event::OnTargetChanged, &a);

        // Log the first fire only: target changes are a per-combat-action event.
        static bool logged = false;
        if (!logged) { logged = true; WLOG_INFO("target: hook live (first change)"); }
        return r;
    }

    /**
     * @brief Detours the play-sound API, emitting OnSoundPlay before the sound starts.
     *
     * Fires on every UI/world sound. Logs the first fire only.
     * @param scriptState  script state the call ran on (the sound id/name is on its stack).
     * @return the native function result.
     */
    int __cdecl hkPlaySound(void* scriptState)
    {
        ev::SoundPlayArgs a{ scriptState };
        ev::Emit(ev::Event::OnSoundPlay, &a);

        static bool logged = false;
        if (!logged) { logged = true; WLOG_INFO("sound: play hook active"); }
        return g_origPlaySound(scriptState);
    }

    /**
     * @brief Diagnostic-only: logs every SoundKitID play attempt and its result code.
     *
     * Disambiguates a data resolution gap (returns 5/6, no file I/O ever attempted) from a
     * cache-served hit (returns success with no corresponding Storage_FileOpen) for the in-world
     * interface/spell/creature sound investigation.
     */
    int __cdecl hkPlaySoundKit(int soundKitId, int p2, int p3, int* p4, int p5,
                               uint32_t* p6, uint32_t p7, int p8)
    {
        int r = g_origPlaySoundKit(soundKitId, p2, p3, p4, p5, p6, p7, p8);
        // Each failing kit id is logged once; the raw stream repeats the same missing ids dozens of
        // times per minute and the per-line file write is itself a frame cost.
        if (r != 0)
        {
            static std::unordered_set<int> loggedKits;
            if (loggedKits.insert(soundKitId).second)
                WLOG_INFO("audio-diag: PlaySoundKit id=%d -> %d", soundKitId, r);
        }
        return r;
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
     * @brief Detours WMO root read-completion, emitting OnWmoRootLoad before the native chunk walk.
     *
     * Fires once after the async read fills the root buffer and before the walker runs, so a subscriber
     * may reshape the root buffer in place; the native walk then reads the reshaped bytes.
     * @param root  map-object root whose buffer was just read.
     */
    void __cdecl hkWmoRootComplete(void* root)
    {
        const uint64_t preStarted = aprof::Now();
        ev::WmoRootLoadArgs a{ root };
        ev::Emit(ev::Event::OnWmoRootLoad, &a);
        if (preStarted) aprof::Record(aprof::Phase::WmoRootPre, aprof::Now() - preStarted);
        const uint64_t nativeStarted = aprof::Now();
        g_origWmoRoot(root);
        if (nativeStarted) aprof::Record(aprof::Phase::WmoRootNative, aprof::Now() - nativeStarted);
    }

    /**
     * @brief Detours WMO group parse, emitting OnWmoGroupLoad before the native sub-chunk walk.
     *
     * The join point of the sync and async group-load paths, before the sub-chunk walk, so a subscriber
     * may reshape the group buffer in place; the native walk then reads the reshaped bytes.
     * @param group  map-object group whose buffer was just read.
     * @param edx    unused register slot for the thiscall convention.
     */
    void __fastcall hkWmoGroupParse(void* group, void* edx)
    {
        const uint64_t preStarted = aprof::Now();
        ev::WmoGroupLoadArgs a{ group };
        ev::Emit(ev::Event::OnWmoGroupLoad, &a);
        if (preStarted) aprof::Record(aprof::Phase::WmoGroupPre, aprof::Now() - preStarted);
        const uint64_t nativeStarted = aprof::Now();
        g_origWmoGroup(group, edx);
        if (nativeStarted) aprof::Record(aprof::Phase::WmoGroupNative, aprof::Now() - nativeStarted);
    }

    /**
     * @brief Detours world enter, emitting OnWorldLeave before and OnWorldEnter after the transition.
     * @param worldTime          target world time.
     * @param withLoadingScreen  nonzero to show the loading screen.
     */
    void __cdecl hkWorldEnter(int worldTime, int withLoadingScreen)
    {
        const auto mapId = static_cast<uint32_t>(*reinterpret_cast<int32_t*>(wld::kCurrentMapId));
        ev::WorldLeaveArgs leave{ mapId }; // old world still loaded: id is the one being left
        ev::Emit(ev::Event::OnWorldLeave, &leave);
        g_origWorldEnter(worldTime, withLoadingScreen);
        wxl::runtime::lua::Install(true);
        const auto entered = static_cast<uint32_t>(*reinterpret_cast<int32_t*>(wld::kCurrentMapId));
        ev::WorldEnterArgs enter{ entered };
        ev::Emit(ev::Event::OnWorldEnter, &enter);
    }

    /**
     * @brief Detours the master per-frame pump, emitting OnUpdate once per frame with the frame delta.
     */
    void __cdecl hkFramePump()
    {
        g_origFramePump();
        ev::UpdateArgs a{ *reinterpret_cast<float*>(frame::kDeltaSeconds),
                          *reinterpret_cast<uint32_t*>(frame::kFrameTimeMs) };
        ev::Emit(ev::Event::OnUpdate, &a);
        aprof::RecordFrame(a.dt);
    }

    /**
     * @brief Detours the CharModel equip-slot handler, emitting OnItemSlotChange then calling the native.
     *
     * Fires when an item is equipped to an internal model slot (not the WoW equipment slot index).
     * modelSlot maps to an equipment category (head, chest, weapon, etc.). itemDataPtr points to
     * the item data block that carries the display_id used to look up ItemDisplayInfo.
     * @param cmo          CharModelObject this pointer.
     * @param edx          thiscall dummy.
     * @param modelSlot    internal model slot index.
     * @param itemDataPtr  item data block pointer (contains display_id).
     * @param postFlag     native post-dispatch flag.
     */
    void __fastcall hkSlotDispatch(void* cmo, void* edx, uint32_t modelSlot, void* itemDataPtr, uint32_t postFlag)
    {
        // Native must run first: for head (slot 11), the native handler checks if slot 11 is occupied and
        // returns NULL if so -- which would skip geoset writes. Let native populate slot 11 first,
        // then subscribers receive the event with the slot already in its post-dispatch state.
        g_origSlotDispatch(cmo, edx, modelSlot, itemDataPtr, postFlag);
        ev::ItemSlotChangeArgs a{ cmo, modelSlot, itemDataPtr };
        ev::Emit(ev::Event::OnItemSlotChange, &a);
    }

    /**
     * @brief Detours the CharModel equip-slot clear, emitting OnItemSlotClear then calling the native.
     *
     * Fires when a WoW equipment slot is cleared on a CharModelObject, detaching any M2 that was
     * loaded for that slot and releasing its render context.
     * @param cmo           CharModelObject this pointer.
     * @param edx           thiscall dummy.
     * @param equipSlotWow  WoW equipment slot index (EQUIPMENT_SLOT_* constants, 0-18).
     */
    void __fastcall hkSlotClear(void* cmo, void* edx, uint32_t equipSlotWow)
    {
        ev::ItemSlotClearArgs a{ cmo, equipSlotWow };
        ev::Emit(ev::Event::OnItemSlotClear, &a);
        g_origSlotClear(cmo, edx, equipSlotWow);
    }

    /**
     * @brief Maps a raw update-field index to a weapon slot (0=mainhand, 1=offhand, 2=ranged).
     * @param fieldIndex  the edx value seen at kUnitFieldSetWrite.
     * @param slotOut     receives the mapped slot on a match.
     * @return false if fieldIndex isn't one of the three weapon-visual entry fields.
     */
    bool ResolveWeaponVisualSlot(uint32_t fieldIndex, uint32_t& slotOut)
    {
        switch (fieldIndex)
        {
            case unit::kFieldVisibleItemMainhandEntry: slotOut = 0; return true;
            case unit::kFieldVisibleItemOffhandEntry:  slotOut = 1; return true;
            case unit::kFieldVisibleItemRangedEntry:   slotOut = 2; return true;
            default: return false;
        }
    }

    /**
     * @brief C++ side of the update-field write capture, called from the naked register-capture stub.
     *
     * Fires on EVERY field write for EVERY object (health, mana, auras, everything) -- the early
     * ResolveWeaponVisualSlot() bail keeps this cheap for the overwhelming majority of calls that
     * aren't weapon-visual fields.
     * @param fieldArrayBase  eax at the write instruction: the object's field array base.
     * @param fieldIndex      edx at the write instruction: which field.
     * @param value           ecx at the write instruction: the new value being committed.
     */
    void __cdecl OnUnitFieldSetCaptured(uint32_t fieldArrayBase, uint32_t fieldIndex, uint32_t value)
    {
        uint32_t slot;
        if (!ResolveWeaponVisualSlot(fieldIndex, slot))
            return;

        // Resolved: fieldArrayBase = unit_ptr + kUnitFieldArrayOffset (confirmed via live diff
        // against the local player's own object pointer, see offsets/game/Unit.hpp).
        void* unitPtr = reinterpret_cast<uint8_t*>(fieldArrayBase) - unit::kUnitFieldArrayOffset;

        ev::WeaponVisualChangeArgs a{ unitPtr, slot, value };
        ev::Emit(ev::Event::OnWeaponVisualChange, &a);
    }

    /**
     * @brief Detours the object update-field commit instruction directly (not a function boundary).
     *
     * Captures eax/ecx/edx exactly as they stand at kUnitFieldSetWrite, forwards them to
     * OnUnitFieldSetCaptured() as plain cdecl arguments, then restores every register and flag and
     * jumps into the trampoline to run the original instruction (and the function's own return)
     * untouched. pushad/popad + pushfd/popfd bracket the call so the original write and its caller
     * never observe that anything intercepted it.
     */
    __declspec(naked) void hkUnitFieldSetWrite()
    {
        __asm
        {
            pushfd
            pushad
            push ecx          ; value          (3rd cdecl arg)
            push edx           ; fieldIndex     (2nd cdecl arg)
            push eax           ; fieldArrayBase (1st cdecl arg)
            call OnUnitFieldSetCaptured
            add esp, 12        ; cdecl: caller cleans the 3 pushed args
            popad
            popfd
            jmp g_origUnitFieldSetWrite
        }
    }

    /**
     * @brief Detours the per-render-ctx M2 scene-graph update, emitting OnM2PerFrameUpdate per visible M2.
     *
     * Fires recursively through the scene graph once per visible M2 render context per frame -- this
     * is the correct hook point for per-frame bone-matrix copy and geoset (index buffer) filtering,
     * both of which must run in step with the render context rather than once per EndScene.
     * @param renderCtx  the M2 render context being updated.
     * @param edx        thiscall dummy.
     */
    void __fastcall hkM2PerFrameUpdate(void* renderCtx, void* edx)
    {
        g_origM2PerFrame(renderCtx, edx);
        // Per-visible-M2-per-frame site: skip the emission entirely while nothing subscribes.
        if (ev::Any(ev::Event::OnM2PerFrameUpdate))
        {
            ev::M2PerFrameUpdateArgs a{ renderCtx };
            ev::Emit(ev::Event::OnM2PerFrameUpdate, &a);
        }
    }

    /**
     * @brief Detours bone-palette build, emitting OnBuildBonePalette after the engine fills the buffer.
     *
     * Called from two sites per collection M2 per frame:
     *   (a) the attached-model update path, inside kM2PerFrameUpdate of the parent character.
     *   (b) The outer scene-traversal loop (0x821B4E), which runs AFTER the parent's PerFrameUpdate.
     *
     * Site (b) overwrites any bone-palette modifications that OnM2PerFrameUpdate subscribers made,
     * reverting the collection M2 to its bind pose every frame. By hooking POST-order here,
     * subscribers can re-apply their modifications immediately after the engine's fill -- guaranteed
     * to be the last write before the GPU upload regardless of scene-list ordering.
     *
     * Calling convention: fastcall, ecx = renderCtx, 5 stack args, ret 0x14 (callee-cleanup).
     */
    void __fastcall hkBuildBonePalette(void* renderCtx, void* edx,
        void* sa1, void* sa2, void* sa3, uint32_t sa4, uint32_t sa5)
    {
        g_origBuildBonePalette(renderCtx, edx, sa1, sa2, sa3, sa4, sa5);
        // Per-instance-per-frame site: skip the emission entirely while nothing subscribes.
        if (ev::Any(ev::Event::OnBuildBonePalette))
        {
            ev::BuildBonePaletteArgs a{ renderCtx };
            ev::Emit(ev::Event::OnBuildBonePalette, &a);
        }
    }

    /**
     * @brief Rejects M2 shadow batches whose palette would overrun WoW's VS constant cache.
     *
     * The native shadow path begins at c31 and copies three float4 registers per bone. The cache contains
     * c0..c255, so 75 bones is the largest representable palette. Retail skins can carry larger batches when
     * a transform/split was skipped or failed; letting the native function process one overwrites the adjacent
     * Gx vertex-declaration table with bone-matrix floats and crashes later in GxPrimVertexPtr.
     */
    void __fastcall hkRenderBatchShadowMap(
        void* instance, void*, uint32_t batchMode, void* skinBatch, void* drawList,
        uint32_t drawIndex, void* skinSection, void* previousSection)
    {
        constexpr uint32_t kMaxShadowBones = (256u - 31u) / 3u;
        uint32_t boneCount = 0;
        __try
        {
            if (skinSection)
                boneCount = *reinterpret_cast<const uint16_t*>(
                    static_cast<const uint8_t*>(skinSection) + 0x0C);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            boneCount = kMaxShadowBones + 1;
        }

        if (boneCount > kMaxShadowBones)
        {
            const uint32_t skipped = ++g_shadowBoneOverflowSkips;
            if (skipped <= 32 || (skipped % 1000u) == 0)
            {
                char path[264] = "<unreadable>";
                __try
                {
                    const auto* bytes = static_cast<const uint8_t*>(instance);
                    const auto* model = bytes ? *reinterpret_cast<void* const*>(bytes + m2::kOffInstModel) : nullptr;
                    if (model)
                    {
                        std::strncpy(path,
                            reinterpret_cast<const char*>(model) + m2::kOffModelPathStem,
                            sizeof(path) - 1);
                        path[sizeof(path) - 1] = '\0';
                    }
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    std::strcpy(path, "<unreadable>");
                }
                WLOG_WARN("M2 shadow: skipped oversized palette bones=%u max=%u draw=%u model='%s' (skips=%u)",
                          boneCount, kMaxShadowBones, drawIndex, path, skipped);
            }
            return;
        }

        g_origRenderBatchShadowMap(instance, batchMode, skinBatch, drawList,
                                   drawIndex, skinSection, previousSection);
    }

}

namespace wxl::runtime::game
{
    void ReserveM2Memory()
    {
        std::lock_guard<std::mutex> lock(g_virtualM2AllocMutex);
        EnsureM2ArenaLocked();
    }

    /**
     * @brief Installs the disk-queue worker-count extension before the client's own boot reaches it.
     *
     * The client creates its disk-queue worker thread very early in boot, well before the render device
     * exists -- installing this hook from the same deferred point every other game-logic detour uses
     * (after the render device shows up) means the client's own call already ran unhooked by the time
     * the hook is armed, so the extra worker threads this detour adds were never actually created. Must
     * run synchronously, before the client's own startup proceeds, same timing requirement as the
     * archive-mount guard. Only extends an already-initialized subsystem after the real call runs
     * (call-original-then-extend); does not touch file-content serving, so it does not carry the
     * separate heap-corruption risk that keeps the content-serving hooks deferred.
     */
    void InstallEarly()
    {
        wxl::core::hook::Install("AsyncFileReadInitialize", wld::kAsyncFileReadInitialize,
                                 reinterpret_cast<void*>(&hkAsyncFileReadInitialize),
                                 reinterpret_cast<void**>(&g_origAsyncFileReadInit));
        wxl::core::hook::Install("AsyncFileReadObject", wld::kAsyncFileReadObject,
                                 reinterpret_cast<void*>(&hkAsyncFileReadObject),
                                 reinterpret_cast<void**>(&g_origAsyncFileReadObject));
        wxl::core::hook::EnableAll();
        WLOG_INFO("game: early hooks installed (AsyncFileReadInitialize, AsyncFileReadObject)");
    }

    /**
     * @brief Installs every game-logic detour through the core hook layer.
     */
    void Install()
    {
        wxl::core::hook::Install("M2BufferAlloc", m2::kBufferAlloc,
                                 reinterpret_cast<void*>(&hkM2BufferAlloc),
                                 reinterpret_cast<void**>(&g_origM2BufferAlloc));
        wxl::core::hook::Install("M2BufferFree", m2::kBufferFree,
                                 reinterpret_cast<void*>(&hkM2BufferFree),
                                 reinterpret_cast<void**>(&g_origM2BufferFree));
        wxl::core::hook::Install("M2Init", m2::kInit,
                                 reinterpret_cast<void*>(&hkM2Init),
                                 reinterpret_cast<void**>(&g_origM2Init));
        wxl::core::hook::Install("M2FinalizeSkin", m2::kFinalizeSkin,
                                 reinterpret_cast<void*>(&hkFinalizeSkin),
                                 reinterpret_cast<void**>(&g_origFinalizeSkin));
        wxl::core::hook::Install("M2BuildBatchMaterial", m2::kBuildBatchMaterial,
                                 reinterpret_cast<void*>(&hkBuildBatchMaterial),
                                 reinterpret_cast<void**>(&g_origBuildBatchMaterial));
        wxl::core::hook::Install("M2SetupBatchAlpha", m2::kSetupBatchAlpha,
                                 reinterpret_cast<void*>(&hkSetupBatchAlpha),
                                 reinterpret_cast<void**>(&g_origSetupAlpha));
        wxl::core::hook::Install("M2SortOpaqueGeoBatches", m2::kSortOpaqueGeoBatches,
                                 reinterpret_cast<void*>(&hkSortOpaqueGeoBatches),
                                 reinterpret_cast<void**>(&g_origSortOpaqueGeoBatches));
        wxl::core::hook::Install("M2SceneTriangleHitTest", m2::kSceneTriangleHitTest,
                                 reinterpret_cast<void*>(&hkSceneTriangleHitTest),
                                 reinterpret_cast<void**>(&g_origSceneTriangleHitTest));
        wxl::core::hook::Install("DoodadSpawn", dd::kSpawnFromMDDF,
                                 reinterpret_cast<void*>(&hkDoodadSpawn),
                                 reinterpret_cast<void**>(&g_origDoodadSpawn));
        wxl::core::hook::Install("WmoSpawn", wmo::kSpawnFromModf,
                                 reinterpret_cast<void*>(&hkWmoSpawn),
                                 reinterpret_cast<void**>(&g_origWmoSpawn));
        wxl::core::hook::Install("TextureUpdate", gxoff::kTextureUpdate,
                                 reinterpret_cast<void*>(&hkTexUpdate),
                                 reinterpret_cast<void**>(&g_origTexUpdate));
        wxl::core::hook::Install("TextureCreate", gxoff::kTextureCreate,
                                 reinterpret_cast<void*>(&hkTexCreate),
                                 reinterpret_cast<void**>(&g_origTexCreate));
        wxl::core::hook::Install("AsyncDrain", wld::kAsyncServiceQueues,
                                 reinterpret_cast<void*>(&hkAsyncDrain),
                                 reinterpret_cast<void**>(&g_origAsyncDrain));
        // AsyncFileReadInitialize / AsyncFileReadObject: installed earlier, see InstallEarly().
        wxl::core::hook::Install("ChunkBuild", adt::kChunkBuild,
                                 reinterpret_cast<void*>(&hkChunkBuild),
                                 reinterpret_cast<void**>(&g_origChunkBuild));
        // ChunkDestroy (ADT cancel-on-teardown) temporarily disabled: it correlates with a render-path
        // null-deref (0x7c846c) and the cancel timing relative to a sibling free is unconfirmed. Re-enable
        // once that ordering is confirmed as teardown rather than a sibling free.
        (void)&hkChunkDestroy; (void)&g_origChunkDestroy;
        wxl::core::hook::Install("WmoRootComplete", wmo::kRootComplete,
                                 reinterpret_cast<void*>(&hkWmoRootComplete),
                                 reinterpret_cast<void**>(&g_origWmoRoot));
        wxl::core::hook::Install("WmoGroupParse", wmo::kGroupParse,
                                 reinterpret_cast<void*>(&hkWmoGroupParse),
                                 reinterpret_cast<void**>(&g_origWmoGroup));
        wxl::core::hook::Install("CWorldEnter", wld::kEnter,
                                 reinterpret_cast<void*>(&hkWorldEnter),
                                 reinterpret_cast<void**>(&g_origWorldEnter));
        wxl::core::hook::Install("FramePump", frame::kFramePump,
                                 reinterpret_cast<void*>(&hkFramePump),
                                 reinterpret_cast<void**>(&g_origFramePump));
        wxl::core::hook::Install("ObjectUpdate", unit::kObjectUpdateHandler,
                                 reinterpret_cast<void*>(&hkObjUpdate),
                                 reinterpret_cast<void**>(&g_origObjUpdate));
        wxl::core::hook::Install("ObjectDestroy", unit::kObjectDestroyHandler,
                                 reinterpret_cast<void*>(&hkObjDestroy),
                                 reinterpret_cast<void**>(&g_origObjDestroy));
        wxl::core::hook::Install("TargetSet", unit::kTargetSet,
                                 reinterpret_cast<void*>(&hkTargetSet),
                                 reinterpret_cast<void**>(&g_origTargetSet));
        wxl::core::hook::Install("PlaySound", snd::kPlaySound,
                                 reinterpret_cast<void*>(&hkPlaySound),
                                 reinterpret_cast<void**>(&g_origPlaySound));
        wxl::core::hook::Install("PlaySoundKit", snd::kPlaySoundKit,
                                 reinterpret_cast<void*>(&hkPlaySoundKit),
                                 reinterpret_cast<void**>(&g_origPlaySoundKit));
        wxl::core::hook::Install("CharModelSlotDispatch", m2::kCharModelSlotDispatch,
                                 reinterpret_cast<void*>(&hkSlotDispatch),
                                 reinterpret_cast<void**>(&g_origSlotDispatch));
        wxl::core::hook::Install("CharModelSlotClear", m2::kCharModelSlotClear,
                                 reinterpret_cast<void*>(&hkSlotClear),
                                 reinterpret_cast<void**>(&g_origSlotClear));
        // Raw instruction hook, not a function boundary -- see the comment on kUnitFieldSetWrite in
        // offsets/game/Unit.hpp. Uses the untyped Install() overload deliberately: hkUnitFieldSetWrite
        // is a naked register-capture stub, not a real C function, so it has no meaningful C++ type to
        // match against the trampoline the way the typed Install<Fn>() overload expects.
        wxl::core::hook::Install("UnitFieldSetWrite", unit::kUnitFieldSetWrite,
                                 reinterpret_cast<void*>(&hkUnitFieldSetWrite),
                                 reinterpret_cast<void**>(&g_origUnitFieldSetWrite));
        wxl::core::hook::Install("M2PerFrameUpdate", m2::kM2PerFrameUpdate,
                                 reinterpret_cast<void*>(&hkM2PerFrameUpdate),
                                 reinterpret_cast<void**>(&g_origM2PerFrame));
        wxl::core::hook::Install("M2BuildBonePalette", m2::kBuildBonePalette,
                                 reinterpret_cast<void*>(&hkBuildBonePalette),
                                 reinterpret_cast<void**>(&g_origBuildBonePalette));
        wxl::core::hook::Install("M2RenderBatchShadowMap", m2::kRenderBatchShadowMap,
                                 reinterpret_cast<void*>(&hkRenderBatchShadowMap),
                                 reinterpret_cast<void**>(&g_origRenderBatchShadowMap));

        // Liquid-row null guard: this one liquid consumer dereferences the LiquidType row flag without the
        // null check the others have, so an unknown liquid id (from any served source) faults. Skip the
        // flag test and make the branch unconditional (nop x4 + jz->jmp) -> default no-bump path.
        {
            const uint8_t guard[5] = { 0x90, 0x90, 0x90, 0x90, 0xEB };
            wxl::core::mem::Patch(reinterpret_cast<void*>(adt::kLiquidRowFlagTest), guard, sizeof guard);
        }

        WLOG_INFO("game: hooks installed (M2BufferAlloc, M2BufferFree, M2Init, M2FinalizeSkin, M2SetupBatchAlpha, M2SortOpaqueGeoBatches, M2RenderBatchShadowMap, DoodadSpawn, TextureUpdate, TextureCreate, ChunkBuild, WmoRootComplete, WmoGroupParse, CWorldEnter, FramePump, ObjectUpdate, ObjectDestroy, TargetSet, PlaySound, PlaySoundKit, CharModelSlotDispatch, CharModelSlotClear, M2PerFrameUpdate, M2BuildBonePalette)");
    }
}
