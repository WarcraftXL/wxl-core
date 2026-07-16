// Coarse client-side asset-load and frame-hitch profiling.
// Copyright (C) 2026 WarcraftXL
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include "runtime/AssetProfile.hpp"

#include "core/Logger.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace wxl::runtime::assetprof
{
    namespace
    {
        constexpr size_t kPhaseCount = static_cast<size_t>(Phase::Count);

        struct Config
        {
            bool enabled = true;
            uint32_t intervalSeconds = 30;
            uint32_t slowMs = 25;
            uint64_t frequency = 1;
            uint64_t intervalTicks = 1;
            uint64_t slowTicks = 1;
        };

        struct PhaseStats
        {
            uint64_t calls = 0;
            uint64_t slow = 0;
            uint64_t ticks = 0;
            uint64_t maxTicks = 0;
            uint64_t units = 0;
        };

        struct Window
        {
            std::array<PhaseStats, kPhaseCount> phases{};
            uint64_t started = 0;
            uint64_t nextReport = 0;
            uint64_t frames = 0;
            uint64_t frameUs = 0;
            uint64_t maxFrameUs = 0;
            uint64_t hitch25 = 0;
            uint64_t hitch50 = 0;
            uint64_t hitch100 = 0;
        };

        SRWLOCK g_lock = SRWLOCK_INIT;
        Window g_window;

        bool ReadEnvU32(const char* name, uint32_t minValue, uint32_t maxValue, uint32_t& out)
        {
            char raw[32];
            const DWORD n = GetEnvironmentVariableA(name, raw, static_cast<DWORD>(sizeof(raw)));
            if (!n || n >= sizeof(raw)) return false;
            char* end = nullptr;
            const unsigned long value = std::strtoul(raw, &end, 10);
            if (end == raw || *end != '\0' || value < minValue || value > maxValue) return false;
            out = static_cast<uint32_t>(value);
            return true;
        }

        const Config& GetConfig()
        {
            static const Config config = [] {
                Config c;
                char enabled[16];
                const DWORD n = GetEnvironmentVariableA(
                    "WXL_CLIENT_ASSET_PROFILE", enabled, static_cast<DWORD>(sizeof(enabled)));
                if (n && n < sizeof(enabled))
                {
                    const int first = std::tolower(static_cast<unsigned char>(enabled[0]));
                    if (first == '0' || first == 'n' || first == 'f') c.enabled = false;
                }
                ReadEnvU32("WXL_CLIENT_ASSET_PROFILE_INTERVAL_SEC", 5, 600, c.intervalSeconds);
                ReadEnvU32("WXL_CLIENT_ASSET_SLOW_MS", 1, 60000, c.slowMs);

                LARGE_INTEGER frequency{};
                if (QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0)
                    c.frequency = static_cast<uint64_t>(frequency.QuadPart);
                c.intervalTicks = c.frequency * c.intervalSeconds;
                c.slowTicks = (c.frequency * c.slowMs + 999) / 1000;
                return c;
            }();
            return config;
        }

        double ToMs(uint64_t ticks)
        {
            return static_cast<double>(ticks) * 1000.0 / static_cast<double>(GetConfig().frequency);
        }

        double AverageMs(const PhaseStats& stats)
        {
            return stats.calls ? ToMs(stats.ticks) / static_cast<double>(stats.calls) : 0.0;
        }

        const PhaseStats& At(const Window& window, Phase phase)
        {
            return window.phases[static_cast<size_t>(phase)];
        }

        void LogWindow(const Window& window, uint64_t elapsedTicks)
        {
            const PhaseStats& m2Pre = At(window, Phase::M2Pre);
            const PhaseStats& m2Native = At(window, Phase::M2Native);
            const PhaseStats& m2Post = At(window, Phase::M2Post);
            const PhaseStats& texRequest = At(window, Phase::TextureRequest);
            const PhaseStats& texUpload = At(window, Phase::TextureUpload);
            const PhaseStats& rootPre = At(window, Phase::WmoRootPre);
            const PhaseStats& rootNative = At(window, Phase::WmoRootNative);
            const PhaseStats& groupPre = At(window, Phase::WmoGroupPre);
            const PhaseStats& groupNative = At(window, Phase::WmoGroupNative);

            wxl::core::log::Printf(
                "asset-prof: win_s=%.1f slow_ms=%u frame[n/h25/h50/h100/avg/max]=%llu/%llu/%llu/%llu/%.2f/%.2f "
                "m2_pre[n/s/total/avg/max]=%llu/%llu/%.2f/%.2f/%.2f "
                "m2_native=%llu/%llu/%.2f/%.2f/%.2f m2_post=%llu/%llu/%.2f/%.2f/%.2f",
                ToMs(elapsedTicks) / 1000.0, GetConfig().slowMs,
                static_cast<unsigned long long>(window.frames),
                static_cast<unsigned long long>(window.hitch25),
                static_cast<unsigned long long>(window.hitch50),
                static_cast<unsigned long long>(window.hitch100),
                window.frames ? static_cast<double>(window.frameUs) / window.frames / 1000.0 : 0.0,
                static_cast<double>(window.maxFrameUs) / 1000.0,
                static_cast<unsigned long long>(m2Pre.calls), static_cast<unsigned long long>(m2Pre.slow),
                ToMs(m2Pre.ticks), AverageMs(m2Pre), ToMs(m2Pre.maxTicks),
                static_cast<unsigned long long>(m2Native.calls), static_cast<unsigned long long>(m2Native.slow),
                ToMs(m2Native.ticks), AverageMs(m2Native), ToMs(m2Native.maxTicks),
                static_cast<unsigned long long>(m2Post.calls), static_cast<unsigned long long>(m2Post.slow),
                ToMs(m2Post.ticks), AverageMs(m2Post), ToMs(m2Post.maxTicks));

            wxl::core::log::Printf(
                "asset-prof-load: tex_request[n/s/total/avg/max]=%llu/%llu/%.2f/%.2f/%.2f "
                "tex_upload[n/s/pixels/total/avg/max]=%llu/%llu/%llu/%.2f/%.2f/%.2f "
                "wmo_root_pre=%llu/%llu/%.2f/%.2f/%.2f wmo_root_native=%llu/%llu/%.2f/%.2f/%.2f "
                "wmo_group_pre=%llu/%llu/%.2f/%.2f/%.2f wmo_group_native=%llu/%llu/%.2f/%.2f/%.2f",
                static_cast<unsigned long long>(texRequest.calls), static_cast<unsigned long long>(texRequest.slow),
                ToMs(texRequest.ticks), AverageMs(texRequest), ToMs(texRequest.maxTicks),
                static_cast<unsigned long long>(texUpload.calls), static_cast<unsigned long long>(texUpload.slow),
                static_cast<unsigned long long>(texUpload.units),
                ToMs(texUpload.ticks), AverageMs(texUpload), ToMs(texUpload.maxTicks),
                static_cast<unsigned long long>(rootPre.calls), static_cast<unsigned long long>(rootPre.slow),
                ToMs(rootPre.ticks), AverageMs(rootPre), ToMs(rootPre.maxTicks),
                static_cast<unsigned long long>(rootNative.calls), static_cast<unsigned long long>(rootNative.slow),
                ToMs(rootNative.ticks), AverageMs(rootNative), ToMs(rootNative.maxTicks),
                static_cast<unsigned long long>(groupPre.calls), static_cast<unsigned long long>(groupPre.slow),
                ToMs(groupPre.ticks), AverageMs(groupPre), ToMs(groupPre.maxTicks),
                static_cast<unsigned long long>(groupNative.calls), static_cast<unsigned long long>(groupNative.slow),
                ToMs(groupNative.ticks), AverageMs(groupNative), ToMs(groupNative.maxTicks));
        }
    }

    uint64_t Now()
    {
        if (!GetConfig().enabled) return 0;
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        return static_cast<uint64_t>(now.QuadPart);
    }

    void Record(Phase phase, uint64_t ticks, uint64_t units)
    {
        if (!GetConfig().enabled) return;
        const size_t index = static_cast<size_t>(phase);
        if (index >= kPhaseCount) return;

        AcquireSRWLockExclusive(&g_lock);
        PhaseStats& stats = g_window.phases[index];
        ++stats.calls;
        stats.ticks += ticks;
        stats.units += units;
        if (ticks >= GetConfig().slowTicks) ++stats.slow;
        stats.maxTicks = std::max(stats.maxTicks, ticks);
        ReleaseSRWLockExclusive(&g_lock);
    }

    void RecordFrame(float deltaSeconds)
    {
        if (!GetConfig().enabled) return;
        const uint64_t now = Now();
        uint64_t frameUs = 0;
        if (std::isfinite(deltaSeconds) && deltaSeconds > 0.0f && deltaSeconds < 10.0f)
            frameUs = static_cast<uint64_t>(deltaSeconds * 1000000.0f + 0.5f);

        Window snapshot;
        uint64_t elapsedTicks = 0;
        bool report = false;
        AcquireSRWLockExclusive(&g_lock);
        if (!g_window.started)
        {
            g_window.started = now;
            g_window.nextReport = now + GetConfig().intervalTicks;
        }
        ++g_window.frames;
        g_window.frameUs += frameUs;
        g_window.maxFrameUs = std::max(g_window.maxFrameUs, frameUs);
        if (frameUs >= 25000) ++g_window.hitch25;
        if (frameUs >= 50000) ++g_window.hitch50;
        if (frameUs >= 100000) ++g_window.hitch100;

        if (now >= g_window.nextReport)
        {
            snapshot = g_window;
            elapsedTicks = now - g_window.started;
            g_window = Window{};
            g_window.started = now;
            g_window.nextReport = now + GetConfig().intervalTicks;
            report = true;
        }
        ReleaseSRWLockExclusive(&g_lock);

        if (report) LogWindow(snapshot, elapsedTicks);
    }
}
