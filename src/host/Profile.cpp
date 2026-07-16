// Low-overhead request profiling for the 64-bit asset host.
// Copyright (C) 2026 WarcraftXL
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include "Profile.hpp"

#include "Host.hpp"
#include "core/Logger.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace wxl::host::profile
{
    namespace
    {
        constexpr size_t kOpCount = static_cast<size_t>(RequestOp::Count);
        constexpr size_t kLatencyBuckets = 9;
        constexpr size_t kSlowSamples = 5;
        constexpr uint32_t kLatencyLimitsMs[kLatencyBuckets - 1] = { 1, 5, 10, 25, 50, 100, 250, 1000 };

        struct OpStats
        {
            uint64_t calls = 0;
            uint64_t bad = 0;
            uint64_t bytes = 0;
            uint64_t ticks = 0;
            uint64_t maxTicks = 0;
        };

        struct SlowSample
        {
            uint64_t totalTicks = 0;
            uint64_t bytes = 0;
            uint64_t providerTicks = 0;
            uint64_t transformCacheTicks = 0;
            uint64_t archiveTicks = 0;
            uint64_t transformTicks = 0;
            uint64_t servedTicks = 0;
            uint64_t blobTicks = 0;
            std::string path;
        };

        struct Window
        {
            uint64_t startTicks = 0;
            std::array<OpStats, kOpCount> ops{};
            std::array<uint64_t, kLatencyBuckets> latency{};
            std::vector<SlowSample> slowest;

            uint64_t requests = 0;
            uint64_t totalTicks = 0;
            uint64_t maxTicks = 0;
            uint64_t postTicks = 0;
            uint64_t badRequests = 0;
            uint64_t slowRequests = 0;

            uint64_t opensOk = 0;
            uint64_t opensMiss = 0;
            OpenTrace open{};
        };

        uint64_t EnvU64(const char* name, uint64_t fallback, uint64_t minValue, uint64_t maxValue)
        {
            const char* raw = std::getenv(name);
            if (!raw || !*raw) return fallback;
            char* end = nullptr;
            uint64_t value = std::strtoull(raw, &end, 10);
            if (end == raw) return fallback;
            return std::clamp(value, minValue, maxValue);
        }

        uint32_t IntervalSeconds()
        {
            static const uint32_t seconds = static_cast<uint32_t>(
                EnvU64("WXL_HOST_PROFILE_INTERVAL_SEC", 30, 5, 600));
            return seconds;
        }

        uint32_t SlowRequestMs()
        {
            static const uint32_t ms = static_cast<uint32_t>(
                EnvU64("WXL_HOST_SLOW_REQUEST_MS", 25, 1, 60000));
            return ms;
        }

        uint64_t Frequency()
        {
            static const uint64_t frequency = [] {
                LARGE_INTEGER f{};
                QueryPerformanceFrequency(&f);
                return static_cast<uint64_t>(f.QuadPart ? f.QuadPart : 1);
            }();
            return frequency;
        }

        double TicksToMs(uint64_t ticks)
        {
            return static_cast<double>(ticks) * 1000.0 / static_cast<double>(Frequency());
        }

        uint64_t MillisecondsToTicks(uint64_t ms)
        {
            return (Frequency() * ms) / 1000;
        }

        Window& Current()
        {
            static Window window;
            return window;
        }

        size_t OpIndex(RequestOp op)
        {
            const size_t index = static_cast<size_t>(op);
            return index < kOpCount ? index : 0;
        }

        void AddOpen(OpenTrace& dst, const OpenTrace& src)
        {
            dst.providerTicks += src.providerTicks;
            dst.transformCacheTicks += src.transformCacheTicks;
            dst.archiveTicks += src.archiveTicks;
            dst.transformTicks += src.transformTicks;
            dst.servedTicks += src.servedTicks;
            dst.blobTicks += src.blobTicks;
            dst.bytes += src.bytes;
            dst.inlineBytes += src.inlineBytes;
            dst.sharedBytes += src.sharedBytes;
            dst.providerCalls += src.providerCalls;
            dst.providerHits += src.providerHits;
            dst.transformCacheLookups += src.transformCacheLookups;
            dst.transformCacheHits += src.transformCacheHits;
            dst.transformCacheStores += src.transformCacheStores;
            dst.archiveReads += src.archiveReads;
            dst.archiveMisses += src.archiveMisses;
            dst.transformCalls += src.transformCalls;
            dst.transformClaims += src.transformClaims;
            dst.servedCalls += src.servedCalls;
            dst.aliasesTried += src.aliasesTried;
            dst.inlineResponses += src.inlineResponses;
            dst.blobCreates += src.blobCreates;
            dst.blobReuses += src.blobReuses;
            dst.blobFailures += src.blobFailures;
        }

        void ConsiderSlowSample(Window& window, const RequestTrace& trace, uint64_t serviceTicks)
        {
            if (trace.op != RequestOp::FileOpen || serviceTicks < MillisecondsToTicks(SlowRequestMs())) return;

            if (window.slowest.size() >= kSlowSamples &&
                serviceTicks <= window.slowest.back().totalTicks) return;

            SlowSample sample;
            sample.totalTicks = serviceTicks;
            sample.bytes = trace.open.bytes;
            sample.providerTicks = trace.open.providerTicks;
            sample.transformCacheTicks = trace.open.transformCacheTicks;
            sample.archiveTicks = trace.open.archiveTicks;
            sample.transformTicks = trace.open.transformTicks;
            sample.servedTicks = trace.open.servedTicks;
            sample.blobTicks = trace.open.blobTicks;
            sample.path = trace.name.substr(0, 220);

            window.slowest.emplace_back(std::move(sample));
            std::sort(window.slowest.begin(), window.slowest.end(),
                [](const SlowSample& a, const SlowSample& b) { return a.totalTicks > b.totalTicks; });
            if (window.slowest.size() > kSlowSamples) window.slowest.resize(kSlowSamples);
        }

        void ResetWindow(uint64_t now)
        {
            Window fresh;
            fresh.startTicks = now;
            Current() = std::move(fresh);
        }
    }

    uint64_t Now()
    {
        if (!wxl::host::ProfilingEnabled()) return 0;
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        return static_cast<uint64_t>(now.QuadPart);
    }

    void RecordRequest(const RequestTrace& trace, uint64_t serviceTicks, uint64_t postResponseTicks)
    {
        if (!wxl::host::ProfilingEnabled()) return;

        Window& window = Current();
        if (!window.startTicks) window.startTicks = Now();

        ++window.requests;
        window.totalTicks += serviceTicks;
        window.maxTicks = std::max(window.maxTicks, serviceTicks);
        window.postTicks += postResponseTicks;
        if (trace.badRequest) ++window.badRequests;
        if (serviceTicks >= MillisecondsToTicks(SlowRequestMs())) ++window.slowRequests;

        OpStats& op = window.ops[OpIndex(trace.op)];
        ++op.calls;
        op.bad += trace.badRequest ? 1 : 0;
        op.bytes += trace.bytes;
        op.ticks += serviceTicks;
        op.maxTicks = std::max(op.maxTicks, serviceTicks);

        const double elapsedMs = TicksToMs(serviceTicks);
        size_t bucket = kLatencyBuckets - 1;
        for (size_t i = 0; i + 1 < kLatencyBuckets; ++i)
        {
            if (elapsedMs <= kLatencyLimitsMs[i]) { bucket = i; break; }
        }
        ++window.latency[bucket];

        if (trace.op == RequestOp::FileOpen)
        {
            window.opensOk += trace.open.ok ? 1 : 0;
            window.opensMiss += trace.open.miss ? 1 : 0;
            AddOpen(window.open, trace.open);
            ConsiderSlowSample(window, trace, serviceTicks);
        }
    }

    bool ReportDue()
    {
        if (!wxl::host::ProfilingEnabled()) return false;
        Window& window = Current();
        const uint64_t now = Now();
        if (!window.startTicks) { window.startTicks = now; return false; }

        const uint64_t elapsedTicks = now - window.startTicks;
        return elapsedTicks >= MillisecondsToTicks(static_cast<uint64_t>(IntervalSeconds()) * 1000);
    }

    void Report(const Gauges& gauges)
    {
        if (!wxl::host::ProfilingEnabled()) return;
        Window& window = Current();
        const uint64_t now = Now();
        if (!window.startTicks) { window.startTicks = now; return; }

        const uint64_t elapsedTicks = now - window.startTicks;

        const double windowSeconds = static_cast<double>(elapsedTicks) / static_cast<double>(Frequency());
        const auto& open = window.ops[OpIndex(RequestOp::FileOpen)];
        const auto& read = window.ops[OpIndex(RequestOp::FileRead)];
        const auto& close = window.ops[OpIndex(RequestOp::FileClose)];
        const auto& exists = window.ops[OpIndex(RequestOp::FileExists)];

        wxl::core::log::Printf(
            "host-prof: window_s=%.1f req=%llu rps=%.1f open=%llu read=%llu close=%llu exists=%llu bad=%llu avg_ms=%.3f max_ms=%.3f slow_ge_%ums=%llu",
            windowSeconds,
            static_cast<unsigned long long>(window.requests),
            windowSeconds > 0.0 ? static_cast<double>(window.requests) / windowSeconds : 0.0,
            static_cast<unsigned long long>(open.calls),
            static_cast<unsigned long long>(read.calls),
            static_cast<unsigned long long>(close.calls),
            static_cast<unsigned long long>(exists.calls),
            static_cast<unsigned long long>(window.badRequests),
            window.requests ? TicksToMs(window.totalTicks) / static_cast<double>(window.requests) : 0.0,
            TicksToMs(window.maxTicks), SlowRequestMs(),
            static_cast<unsigned long long>(window.slowRequests));

        wxl::core::log::Printf(
            "host-prof-open: ok=%llu miss=%llu bytes_mb=%.1f inline_mb=%.1f shm_mb=%.1f provider=%u/%u xcache=%u/%u xcache_store=%u archive=%u/%u transform=%u/%u notify=%u aliases=%u blob_new=%u blob_reuse=%u blob_fail=%u",
            static_cast<unsigned long long>(window.opensOk),
            static_cast<unsigned long long>(window.opensMiss),
            static_cast<double>(window.open.bytes) / (1024.0 * 1024.0),
            static_cast<double>(window.open.inlineBytes) / (1024.0 * 1024.0),
            static_cast<double>(window.open.sharedBytes) / (1024.0 * 1024.0),
            window.open.providerHits, window.open.providerCalls,
            window.open.transformCacheHits, window.open.transformCacheLookups,
            window.open.transformCacheStores,
            window.open.archiveReads - window.open.archiveMisses, window.open.archiveReads,
            window.open.transformClaims, window.open.transformCalls,
            window.open.servedCalls, window.open.aliasesTried,
            window.open.blobCreates, window.open.blobReuses, window.open.blobFailures);

        const uint64_t measuredOpenTicks = window.open.providerTicks + window.open.transformCacheTicks +
            window.open.archiveTicks + window.open.transformTicks + window.open.servedTicks + window.open.blobTicks;
        const uint64_t knownTicks = measuredOpenTicks + window.postTicks;
        const uint64_t otherTicks = window.totalTicks > knownTicks ? window.totalTicks - knownTicks : 0;
        wxl::core::log::Printf(
            "host-prof-stage-ms: provider=%.2f xcache=%.2f archive=%.2f transform=%.2f notify=%.2f blob=%.2f post=%.2f other=%.2f",
            TicksToMs(window.open.providerTicks), TicksToMs(window.open.transformCacheTicks),
            TicksToMs(window.open.archiveTicks), TicksToMs(window.open.transformTicks),
            TicksToMs(window.open.servedTicks), TicksToMs(window.open.blobTicks),
            TicksToMs(window.postTicks), TicksToMs(otherTicks));

        wxl::core::log::Printf(
            "host-prof-memory: xcache_entries=%zu xcache_mb=%.1f blobs=%zu blob_mb=%.1f blob_refs=%llu",
            gauges.transformCacheEntries,
            static_cast<double>(gauges.transformCacheBytes) / (1024.0 * 1024.0),
            gauges.blobs, static_cast<double>(gauges.blobBytes) / (1024.0 * 1024.0),
            static_cast<unsigned long long>(gauges.blobRefs));

        wxl::core::log::Printf(
            "host-prof-latency: le1=%llu le5=%llu le10=%llu le25=%llu le50=%llu le100=%llu le250=%llu le1000=%llu gt1000=%llu",
            static_cast<unsigned long long>(window.latency[0]),
            static_cast<unsigned long long>(window.latency[1]),
            static_cast<unsigned long long>(window.latency[2]),
            static_cast<unsigned long long>(window.latency[3]),
            static_cast<unsigned long long>(window.latency[4]),
            static_cast<unsigned long long>(window.latency[5]),
            static_cast<unsigned long long>(window.latency[6]),
            static_cast<unsigned long long>(window.latency[7]),
            static_cast<unsigned long long>(window.latency[8]));

        for (size_t i = 0; i < window.slowest.size(); ++i)
        {
            const SlowSample& s = window.slowest[i];
            wxl::core::log::Printf(
                "host-prof-slow: rank=%zu total_ms=%.2f provider=%.2f xcache=%.2f archive=%.2f transform=%.2f notify=%.2f blob=%.2f bytes=%llu path='%s'",
                i + 1, TicksToMs(s.totalTicks), TicksToMs(s.providerTicks),
                TicksToMs(s.transformCacheTicks), TicksToMs(s.archiveTicks),
                TicksToMs(s.transformTicks), TicksToMs(s.servedTicks), TicksToMs(s.blobTicks),
                static_cast<unsigned long long>(s.bytes), s.path.c_str());
        }

        ResetWindow(now);
    }

    void LogSettings()
    {
        wxl::core::log::Printf("host-prof: enabled=%u interval_s=%u slow_ms=%u samples=%zu",
            wxl::host::ProfilingEnabled() ? 1u : 0u, IntervalSeconds(), SlowRequestMs(), kSlowSamples);
    }
}
