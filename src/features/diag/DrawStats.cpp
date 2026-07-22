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

#include "features/diag/DrawStats.hpp"

#include "common/Log.hpp"

#include <atomic>
#include <mutex>

namespace
{
    // Aggregate behind Get(). Written once per frame by the render thread, read from the Lua thread.
    // A plain mutex is right here: the contention window is a struct copy once per frame.
    std::mutex                             g_mutex;
    wxl::runtime::drawstats::FrameCounters g_last{};
    wxl::runtime::drawstats::FrameCounters g_peak{};
    uint64_t                               g_sumDraws   = 0;
    uint64_t                               g_sumM2Draws = 0;
    uint32_t                               g_frames     = 0;

    std::atomic<uint32_t> g_modelsFinalized{ 0 };
    std::atomic<uint32_t> g_modelsNonBatchable{ 0 };
    std::atomic<uint32_t> g_minMaxInstances{ 0xFFFFFFFFu };
}

namespace wxl::runtime::drawstats
{
    FrameCounters g_live{};

    void EndFrame() noexcept
    {
        const FrameCounters f = g_live;
        g_live = FrameCounters{};

        // A frame that drew nothing (loading screen, minimised) would drag the averages down and says
        // nothing about render cost -- skip it rather than pollute the sample.
        if (f.draws == 0) return;

        std::lock_guard<std::mutex> lock(g_mutex);
        g_last = f;
        if (f.draws   > g_peak.draws)   g_peak.draws   = f.draws;
        if (f.m2Draws > g_peak.m2Draws) g_peak.m2Draws = f.m2Draws;
        if (f.tris    > g_peak.tris)    g_peak.tris    = f.tris;
        g_sumDraws   += f.draws;
        g_sumM2Draws += f.m2Draws;
        ++g_frames;
    }

    void RecordFinalize(uint32_t maxInstances) noexcept
    {
        g_modelsFinalized.fetch_add(1, std::memory_order_relaxed);
        // < 2 is the exact condition CM2Model::InitializeLoaded (0x00832EA0) uses to clear the
        // "batchable doodad" flag 0x10, so this counts models that cost one draw call per placement.
        if (maxInstances < 2) g_modelsNonBatchable.fetch_add(1, std::memory_order_relaxed);

        uint32_t prev = g_minMaxInstances.load(std::memory_order_relaxed);
        while (maxInstances < prev &&
               !g_minMaxInstances.compare_exchange_weak(prev, maxInstances, std::memory_order_relaxed))
        {
        }
    }

    Stats Get() noexcept
    {
        Stats s{};
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            s.last          = g_last;
            s.peak          = g_peak;
            s.framesSampled = g_frames;
            s.avgDraws      = g_frames ? static_cast<double>(g_sumDraws)   / g_frames : 0.0;
            s.avgM2Draws    = g_frames ? static_cast<double>(g_sumM2Draws) / g_frames : 0.0;
        }
        s.modelsFinalized    = g_modelsFinalized.load(std::memory_order_relaxed);
        s.modelsNonBatchable = g_modelsNonBatchable.load(std::memory_order_relaxed);
        s.minMaxInstances    = g_minMaxInstances.load(std::memory_order_relaxed);
        return s;
    }

    void Reset() noexcept
    {
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_last = FrameCounters{};
            g_peak = FrameCounters{};
            g_sumDraws = g_sumM2Draws = 0;
            g_frames = 0;
        }
        g_modelsFinalized.store(0, std::memory_order_relaxed);
        g_modelsNonBatchable.store(0, std::memory_order_relaxed);
        g_minMaxInstances.store(0xFFFFFFFFu, std::memory_order_relaxed);
        WLOG_INFO("drawstats: counters reset");
    }
}
