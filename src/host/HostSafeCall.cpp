// Host hook safe-call: runs each hook under a crash guard, quarantines faults, and times invocations.
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

#include "HostHooks.hpp"

#include "common/Log.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <chrono>
#include <thread>
#endif

#if defined(_MSC_VER) && defined(_WIN32)
#include <excpt.h>
#endif

#include <algorithm>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_set>

namespace wxl::host
{
    HookProfileGuard::HookProfileGuard(HookProfile& profile) : profile_(profile)
    {
        if (!profile_.guard.test_and_set(std::memory_order_acquire)) return;

        do
        {
#if defined(_WIN32)
            YieldProcessor();
#else
            std::this_thread::yield();
#endif
            while (profile_.guard.test(std::memory_order_relaxed))
            {
#if defined(_WIN32)
                YieldProcessor();
#else
                std::this_thread::yield();
#endif
            }
        }
        while (profile_.guard.test_and_set(std::memory_order_acquire));
    }

    HookProfileGuard::~HookProfileGuard()
    {
        profile_.guard.clear(std::memory_order_release);
    }

    namespace
    {
        /** @brief Returns hook/path pairs that faulted once and should be skipped for this host session. */
        std::unordered_set<std::string>& FaultedHooks() { static std::unordered_set<std::string> v; return v; }
        /** @brief Protects the faulted-hook set; host requests can arrive concurrently. */
        std::mutex& FaultedHooksMutex() { static std::mutex m; return m; }
        /** @brief Number of unique quarantined hook/path pairs; zero enables the lock-free healthy path. */
        std::atomic<uint64_t>& FaultedHookCount() { static std::atomic<uint64_t> n{ 0 }; return n; }

        /** @brief Returns a high-resolution timestamp in platform performance-counter ticks. */
        uint64_t PerformanceTicks()
        {
#if defined(_WIN32)
            LARGE_INTEGER ticks{};
            QueryPerformanceCounter(&ticks);
            return static_cast<uint64_t>(ticks.QuadPart);
#else
            return static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
#endif
        }

        /** @brief Adds one safe-wrapper invocation to a hook's current profiling window. */
        void RecordProfile(HookProfile& profile, uint64_t ticks, bool claimed, bool faulted, bool skipped)
        {
            HookProfileGuard lock(profile);
            ++profile.calls;
            if (claimed) ++profile.claimed;
            if (faulted) ++profile.faults;
            if (skipped) ++profile.skips;
            profile.totalTicks += ticks;
            profile.maxTicks = std::max(profile.maxTicks, ticks);
        }

        /** @brief Finishes a timed safe-wrapper invocation when profiling is enabled. */
        template <class Fn>
        void FinishProfile(const Hook<Fn>& hook, bool enabled, uint64_t started,
                           bool claimed, bool faulted, bool skipped)
        {
            if (!enabled) return;
            RecordProfile(*hook.profile, PerformanceTicks() - started, claimed, faulted, skipped);
        }

        /** @brief Builds a stable key for a hook handling a path. */
        std::string FaultKey(const char* phase, const char* hookName, std::string_view name)
        {
            std::string key;
            key.reserve((phase ? strlen(phase) : 0) + (hookName ? strlen(hookName) : 0) + name.size() + 2);
            if (phase) key.append(phase);
            key.push_back('|');
            if (hookName) key.append(hookName);
            key.push_back('|');
            key.append(name.data() ? name.data() : "", name.size());
            return key;
        }

        /** @brief Reports whether this hook/path pair already crashed. */
        bool IsHookFaulted(const char* phase, const char* hookName, std::string_view name)
        {
            if (FaultedHookCount().load(std::memory_order_acquire) == 0) return false;
            const std::string key = FaultKey(phase, hookName, name);
            std::lock_guard<std::mutex> lock(FaultedHooksMutex());
            return FaultedHooks().find(key) != FaultedHooks().end();
        }

        /** @brief Remembers a hook/path crash so repeated opens do not re-enter unsafe code. */
        void MarkHookFaulted(const char* phase, const char* hookName, std::string_view name)
        {
            const std::string key = FaultKey(phase, hookName, name);
            std::lock_guard<std::mutex> lock(FaultedHooksMutex());
            if (FaultedHooks().insert(key).second)
                FaultedHookCount().fetch_add(1, std::memory_order_release);
        }

        /** @brief Emits a concise crash line for a host hook without assuming the path is null-terminated. */
        void LogHookFault(const char* phase, const char* hookName, std::string_view name)
        {
            const size_t n = std::min<size_t>(name.size(), 180);
            WLOG_INFO("host: %s hook '%s' crashed while handling '%.*s'; skipping",
                phase ? phase : "unknown",
                hookName ? hookName : "(unnamed)",
                static_cast<int>(n),
                name.data() ? name.data() : "");
        }
    }

#if defined(_MSC_VER) && defined(_WIN32)
    bool SafeProvide(const Hook<ProvideFn>& h, std::string_view name, std::vector<uint8_t>& out)
    {
        const bool profiling = ProfilingEnabled();
        const uint64_t started = profiling ? PerformanceTicks() : 0;
        if (IsHookFaulted("provider", h.name, name))
        {
            FinishProfile(h, profiling, started, false, false, true);
            return false;
        }
        bool claimed = false;
        bool faulted = false;
        __try
        {
            claimed = h.fn(name, out);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            out.clear();
            LogHookFault("provider", h.name, name);
            MarkHookFaulted("provider", h.name, name);
            claimed = false;
            faulted = true;
        }
        FinishProfile(h, profiling, started, claimed, faulted, false);
        return claimed;
    }

    bool SafeTransform(const Hook<TransformFn>& h, std::string_view name, std::span<const uint8_t> raw, std::vector<uint8_t>& out)
    {
        const bool profiling = ProfilingEnabled();
        const uint64_t started = profiling ? PerformanceTicks() : 0;
        if (IsHookFaulted("transform", h.name, name))
        {
            FinishProfile(h, profiling, started, false, false, true);
            return false;
        }
        bool claimed = false;
        bool faulted = false;
        __try
        {
            claimed = h.fn(name, raw, out);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            out.clear();
            LogHookFault("transform", h.name, name);
            MarkHookFaulted("transform", h.name, name);
            claimed = false;
            faulted = true;
        }
        FinishProfile(h, profiling, started, claimed, faulted, false);
        return claimed;
    }

    bool SafeExists(const Hook<ExistsFn>& h, std::string_view name)
    {
        const bool profiling = ProfilingEnabled();
        const uint64_t started = profiling ? PerformanceTicks() : 0;
        if (IsHookFaulted("exists", h.name, name))
        {
            FinishProfile(h, profiling, started, false, false, true);
            return false;
        }
        bool exists = false;
        bool faulted = false;
        __try
        {
            exists = h.fn(name);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            LogHookFault("exists", h.name, name);
            MarkHookFaulted("exists", h.name, name);
            exists = false;
            faulted = true;
        }
        FinishProfile(h, profiling, started, exists, faulted, false);
        return exists;
    }

    void SafeServed(const Hook<ServedFn>& h, std::string_view name, std::span<const uint8_t> bytes)
    {
        const bool profiling = ProfilingEnabled();
        const uint64_t started = profiling ? PerformanceTicks() : 0;
        if (IsHookFaulted("served", h.name, name))
        {
            FinishProfile(h, profiling, started, false, false, true);
            return;
        }
        bool faulted = false;
        __try
        {
            h.fn(name, bytes);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            LogHookFault("served", h.name, name);
            MarkHookFaulted("served", h.name, name);
            faulted = true;
        }
        FinishProfile(h, profiling, started, false, faulted, false);
    }

    bool SafeResolve(const Hook<ResolveFn>& h, uint32_t fileDataId, std::string& outPath)
    {
        const bool profiling = ProfilingEnabled();
        const uint64_t started = profiling ? PerformanceTicks() : 0;
        bool resolved = false;
        bool faulted = false;
        __try
        {
            resolved = h.fn(fileDataId, outPath);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            outPath.clear();
            WLOG_INFO("host: resolver '%s' crashed while handling FileDataID %u; skipping",
                h.name ? h.name : "(unnamed)", fileDataId);
            resolved = false;
            faulted = true;
        }
        FinishProfile(h, profiling, started, resolved, faulted, false);
        return resolved;
    }
#else
    bool SafeProvide(const Hook<ProvideFn>& h, std::string_view name, std::vector<uint8_t>& out)
    {
        const bool profiling = ProfilingEnabled();
        const uint64_t started = profiling ? PerformanceTicks() : 0;
        if (IsHookFaulted("provider", h.name, name))
        {
            FinishProfile(h, profiling, started, false, false, true);
            return false;
        }
        try
        {
            const bool claimed = h.fn(name, out);
            FinishProfile(h, profiling, started, claimed, false, false);
            return claimed;
        }
        catch (...)
        {
            out.clear();
            LogHookFault("provider", h.name, name);
            MarkHookFaulted("provider", h.name, name);
            FinishProfile(h, profiling, started, false, true, false);
            return false;
        }
    }

    bool SafeTransform(const Hook<TransformFn>& h, std::string_view name, std::span<const uint8_t> raw, std::vector<uint8_t>& out)
    {
        const bool profiling = ProfilingEnabled();
        const uint64_t started = profiling ? PerformanceTicks() : 0;
        if (IsHookFaulted("transform", h.name, name))
        {
            FinishProfile(h, profiling, started, false, false, true);
            return false;
        }
        try
        {
            const bool claimed = h.fn(name, raw, out);
            FinishProfile(h, profiling, started, claimed, false, false);
            return claimed;
        }
        catch (...)
        {
            out.clear();
            LogHookFault("transform", h.name, name);
            MarkHookFaulted("transform", h.name, name);
            FinishProfile(h, profiling, started, false, true, false);
            return false;
        }
    }

    bool SafeExists(const Hook<ExistsFn>& h, std::string_view name)
    {
        const bool profiling = ProfilingEnabled();
        const uint64_t started = profiling ? PerformanceTicks() : 0;
        if (IsHookFaulted("exists", h.name, name))
        {
            FinishProfile(h, profiling, started, false, false, true);
            return false;
        }
        try
        {
            const bool exists = h.fn(name);
            FinishProfile(h, profiling, started, exists, false, false);
            return exists;
        }
        catch (...)
        {
            LogHookFault("exists", h.name, name);
            MarkHookFaulted("exists", h.name, name);
            FinishProfile(h, profiling, started, false, true, false);
            return false;
        }
    }

    void SafeServed(const Hook<ServedFn>& h, std::string_view name, std::span<const uint8_t> bytes)
    {
        const bool profiling = ProfilingEnabled();
        const uint64_t started = profiling ? PerformanceTicks() : 0;
        if (IsHookFaulted("served", h.name, name))
        {
            FinishProfile(h, profiling, started, false, false, true);
            return;
        }
        try
        {
            h.fn(name, bytes);
            FinishProfile(h, profiling, started, false, false, false);
        }
        catch (...)
        {
            LogHookFault("served", h.name, name);
            MarkHookFaulted("served", h.name, name);
            FinishProfile(h, profiling, started, false, true, false);
        }
    }

    bool SafeResolve(const Hook<ResolveFn>& h, uint32_t fileDataId, std::string& outPath)
    {
        const bool profiling = ProfilingEnabled();
        const uint64_t started = profiling ? PerformanceTicks() : 0;
        try
        {
            const bool resolved = h.fn(fileDataId, outPath);
            FinishProfile(h, profiling, started, resolved, false, false);
            return resolved;
        }
        catch (...)
        {
            outPath.clear();
            WLOG_INFO("host: resolver '%s' crashed while handling FileDataID %u; skipping",
                h.name ? h.name : "(unnamed)", fileDataId);
            FinishProfile(h, profiling, started, false, true, false);
            return false;
        }
    }
#endif
}
