// Per-frame draw-call accounting and M2 doodad-batching diagnosis.
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

/**
 * @brief Counts what the frame actually costs the device, and why M2 doodads may not be batching.
 *
 * Two independent questions this answers, both raised by an unexplained 180 -> 50 fps drop on a
 * split-ADT map full of natively-read modern M2s:
 *
 *  1. HOW MANY DRAW CALLS does a frame issue, and what share of them are M2 batches? Counted in the
 *     DrawIndexedPrimitive detour the render feature already owns (features/render/M2Draw.cpp), which
 *     knows whether a draw is happening inside an M2 batch because it tracks the drawing model.
 *
 *  2. IS DOODAD BATCHING OFF? CM2Shared::InitializeSkinProfile (kFinalizeSkin, 0x00837A40) computes
 *     "max instances per batched draw" into shared+0x194 (kOffSharedMaxInstances) as
 *       min(65536 / skin->indices.count, min over submeshes of boneCountMax / submesh.boneCount)
 *     and CM2Model::InitializeLoaded (0x00832EA0) CLEARS the model's "batchable doodad" flag 0x10
 *     when that value is < 2 (cf. CVar M2BatchDoodads, "combine doodads to reduce batch count").
 *     A dense modern doodad therefore silently opts itself out of instanced batching and costs one
 *     draw call per placement. Recording the value at finalize tells us how much of the corpus does.
 *
 * Everything here is compiled in only under features::kDiag and the hot path is three increments on
 * plain render-thread-local counters; the atomics are touched once per frame, not once per draw.
 */
namespace wxl::runtime::drawstats
{
    /** @brief What one frame issued. */
    struct FrameCounters
    {
        uint32_t draws;   ///< every DrawIndexedPrimitive
        uint32_t m2Draws; ///< those issued while an M2 batch draw was on the stack
        uint32_t tris;    ///< summed primitive counts
    };

    /** @brief Aggregated session view, safe to read from another thread. */
    struct Stats
    {
        FrameCounters last;              ///< most recently completed frame
        FrameCounters peak;              ///< worst frame seen since the last Reset
        double        avgDraws;          ///< mean draws per sampled frame
        double        avgM2Draws;        ///< mean M2 draws per sampled frame
        uint32_t      framesSampled;

        uint32_t      modelsFinalized;   ///< skin finalizes observed
        uint32_t      modelsNonBatchable;///< of those, shared+0x194 < 2 -> flag 0x10 cleared
        uint32_t      minMaxInstances;   ///< smallest shared+0x194 seen (0xFFFFFFFF when none yet)
    };

    /// Render-thread-only live counters. Not atomic on purpose: only the render thread writes them,
    /// and EndFrame (same thread) is the sole publisher into the atomics behind Get().
    extern FrameCounters g_live;

    /** @brief Hot path: one draw call went to the device. */
    inline void CountDraw(bool isM2, uint32_t primCount) noexcept
    {
        ++g_live.draws;
        if (isM2) ++g_live.m2Draws;
        g_live.tris += primCount;
    }

    /** @brief Frame boundary: publishes the live counters into the aggregate and zeroes them. */
    void EndFrame() noexcept;

    /** @brief Records one skin finalize's shared+0x194 "max instances per batched draw". */
    void RecordFinalize(uint32_t maxInstances) noexcept;

    /** @brief Snapshot of the aggregate. */
    Stats Get() noexcept;

    /** @brief Clears the aggregate (peaks, averages, batching tallies) so a zone can be measured alone. */
    void Reset() noexcept;
}
