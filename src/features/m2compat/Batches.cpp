// M2 batch compatibility: hit-test/opaque-sort SEH guards, alpha-setup event and per-frame scene update.
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
#include "offsets/game/M2.hpp"

#include <windows.h>

#include <atomic>
#include <cstdint>

namespace
{
    namespace ev = wxl::events;
    namespace m2 = wxl::offsets::game::m2;

    m2::M2_SceneTriangleHitTestFn g_origSceneTriangleHitTest = nullptr;
    m2::M2_SortOpaqueGeoBatchesFn g_origSortOpaqueGeoBatches = nullptr;
    m2::M2_SetupBatchAlphaFn      g_origSetupAlpha           = nullptr;
    m2::M2_PerFrameUpdateFn       g_origM2PerFrame           = nullptr;
    std::atomic<uint32_t>         g_sceneHitTestFaults{ 0 };
    std::atomic<uint32_t>         g_opaqueSortFaults{ 0 };

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
                scratch, nullptr, indexBegin, indexEnd, vertexBase, point, mode, candidate, bestDepth, currentHit);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            const uint32_t faults = g_sceneHitTestFaults.fetch_add(1, std::memory_order_relaxed) + 1;
            if (faults == 1 || (faults & (faults - 1)) == 0)
                WLOG_WARN("M2 scene hit-test skipped stale collision data (faults=%u)", faults);
            return currentHit;
        }
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

    bool InstallM2CompatBatches()
    {
        // thiscall target hooked through a fastcall+edx trampoline: the detour type cannot match the
        // native typedef, so the untyped install primitive is required here.
        wxl::hook::Install("M2SceneTriangleHitTest", m2::kSceneTriangleHitTest,
                           &hkSceneTriangleHitTest, &g_origSceneTriangleHitTest);
        wxl::hook::Install("M2SortOpaqueGeoBatches", m2::kSortOpaqueGeoBatches,
                           &hkSortOpaqueGeoBatches, &g_origSortOpaqueGeoBatches);
        wxl::hook::Install("M2SetupBatchAlpha", m2::kSetupBatchAlpha,
                           &hkSetupBatchAlpha, &g_origSetupAlpha);
        wxl::hook::Install("M2PerFrameUpdate", m2::kM2PerFrameUpdate,
                           &hkM2PerFrameUpdate, &g_origM2PerFrame);
        return true;
    }
}

WXL_REGISTER_FEATURE("m2compat-batches", wxl::features::kM2Compat, InstallM2CompatBatches)
