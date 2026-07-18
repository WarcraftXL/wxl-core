// Internal hook types shared by the host registry and its crash-isolating safe-call wrappers.
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

#include "Host.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Private to the host binary (not part of the public Host.hpp surface). The registry (Host.cpp) owns the
// hook lists and profiling report; the safe-call wrappers (HostSafeCall.cpp) run each hook under a crash
// guard and record its timing. Both need these types, so they live here rather than in either .cpp.
namespace wxl::host
{
    /** @brief Coherently updated counters collected for one hook between profile summaries. */
    struct HookProfile
    {
        std::atomic_flag guard = ATOMIC_FLAG_INIT;
        uint64_t calls = 0;
        uint64_t claimed = 0;
        uint64_t totalTicks = 0;
        uint64_t maxTicks = 0;
        uint64_t faults = 0;
        uint64_t skips = 0;
    };

    /** @brief Holds a hook profile's short update/snapshot critical section (spin lock; defined in HostSafeCall.cpp). */
    class HookProfileGuard
    {
    public:
        explicit HookProfileGuard(HookProfile& profile);
        ~HookProfileGuard();

        HookProfileGuard(const HookProfileGuard&) = delete;
        HookProfileGuard& operator=(const HookProfileGuard&) = delete;

    private:
        HookProfile& profile_;
    };

    /** @brief Pairs a hook callback with its display name. */
    template <class Fn> struct Hook
    {
        const char* name;
        Fn fn;
        std::unique_ptr<HookProfile> profile;

        Hook(const char* hookName, Fn callback)
            : name(hookName), fn(callback), profile(std::make_unique<HookProfile>()) {}

        Hook(Hook&&) noexcept = default;
        Hook& operator=(Hook&&) noexcept = default;
        Hook(const Hook&) = delete;
        Hook& operator=(const Hook&) = delete;
    };

    // Crash-isolating invokers: each runs one hook under a SEH/catch guard, quarantines a hook/path pair
    // that faults so repeated opens do not re-enter it, and records timing when profiling is enabled.

    bool SafeProvide(const Hook<ProvideFn>& h, std::string_view name, std::vector<uint8_t>& out);
    bool SafeTransform(const Hook<TransformFn>& h, std::string_view name, std::span<const uint8_t> raw,
                       std::vector<uint8_t>& out);
    bool SafeExists(const Hook<ExistsFn>& h, std::string_view name);
    void SafeServed(const Hook<ServedFn>& h, std::string_view name, std::span<const uint8_t> bytes);
    bool SafeResolve(const Hook<ResolveFn>& h, uint32_t fileDataId, std::string& outPath);
}
