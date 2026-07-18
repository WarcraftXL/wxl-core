// Host hook registry: registers hooks, dispatches the serve pipeline through them, and reports profiling.
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

#include "Host.hpp"

#include "HostHooks.hpp"
#include "common/Config.hpp"
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
#endif

#include <cstdint>
#include <vector>

namespace wxl::host
{
    namespace
    {
        // Function-local statics so a module file-scope registrar can register before main runs without a
        // static-init-order race against these lists.

        /** @brief Returns the transform hook list. */
        std::vector<Hook<TransformFn>>& Transforms() { static std::vector<Hook<TransformFn>> v; return v; }
        /** @brief Returns the provider hook list. */
        std::vector<Hook<ProvideFn>>&   Providers()  { static std::vector<Hook<ProvideFn>>   v; return v; }
        /** @brief Returns the exists hook list. */
        std::vector<Hook<ExistsFn>>&    Existers()   { static std::vector<Hook<ExistsFn>>    v; return v; }
        /** @brief Returns the served observer hook list. */
        std::vector<Hook<ServedFn>>&    Serveds()    { static std::vector<Hook<ServedFn>>    v; return v; }
        /** @brief Returns the FileDataID resolver list. */
        std::vector<Hook<ResolveFn>>&   Resolvers()  { static std::vector<Hook<ResolveFn>>   v; return v; }

        /** @brief Returns the number of performance-counter ticks per second. */
        uint64_t PerformanceFrequency()
        {
#if defined(_WIN32)
            static const uint64_t frequency = []
            {
                LARGE_INTEGER value{};
                QueryPerformanceFrequency(&value);
                return static_cast<uint64_t>(value.QuadPart);
            }();
            return frequency;
#else
            using Period = std::chrono::steady_clock::period;
            return static_cast<uint64_t>(Period::den / Period::num);
#endif
        }

        /** @brief Takes and logs one coherent snapshot of a hook's completed profiling window. */
        template <class Fn>
        void LogAndResetProfile(const char* phase, const Hook<Fn>& hook, uint64_t frequency)
        {
            HookProfile& profile = *hook.profile;
            uint64_t calls;
            uint64_t claimed;
            uint64_t totalTicks;
            uint64_t maxTicks;
            uint64_t faults;
            uint64_t skips;
            {
                HookProfileGuard lock(profile);
                calls = profile.calls;
                claimed = profile.claimed;
                totalTicks = profile.totalTicks;
                maxTicks = profile.maxTicks;
                faults = profile.faults;
                skips = profile.skips;

                profile.calls = 0;
                profile.claimed = 0;
                profile.totalTicks = 0;
                profile.maxTicks = 0;
                profile.faults = 0;
                profile.skips = 0;
            }
            if (calls == 0) return;

            const double ticksToMs = frequency ? 1000.0 / static_cast<double>(frequency) : 0.0;
            const double totalMs = static_cast<double>(totalTicks) * ticksToMs;
            const double averageMs = totalMs / static_cast<double>(calls);
            const double maximumMs = static_cast<double>(maxTicks) * ticksToMs;
            WLOG_INFO(
                "host-prof-hook: phase=%s name='%s' calls=%llu claimed=%llu total_ms=%.3f avg_ms=%.3f max_ms=%.3f faults=%llu skips=%llu",
                phase,
                hook.name ? hook.name : "(unnamed)",
                static_cast<unsigned long long>(calls),
                static_cast<unsigned long long>(claimed),
                totalMs,
                averageMs,
                maximumMs,
                static_cast<unsigned long long>(faults),
                static_cast<unsigned long long>(skips));
        }

        /** @brief Logs and resets every hook in one registration phase. */
        template <class Fn>
        void LogAndResetProfiles(const char* phase, const std::vector<Hook<Fn>>& hooks, uint64_t frequency)
        {
            for (const auto& hook : hooks) LogAndResetProfile(phase, hook, frequency);
        }
    }

    /** @brief Reports whether per-hook profiling is enabled by the environment (default on). */
    bool ProfilingEnabled()
    {
        static const bool enabled = wxl::config::Env("WXL_HOST_PROFILE", true);
        return enabled;
    }

    /**
     * @brief Appends a transform hook to the list, ignoring a null callback.
     * @param name  display name for the hook
     * @param fn    transform callback
     */
    void RegisterTransform(const char* name, TransformFn fn)
    {
        if (!fn) return;
        Transforms().emplace_back(name, fn);
        WLOG_INFO("host: + transform hook '%s'", name ? name : "(unnamed)");
    }

    /**
     * @brief Appends a provider hook to the list, ignoring a null callback.
     * @param name  display name for the hook
     * @param fn    provider callback
     */
    void RegisterProvider(const char* name, ProvideFn fn)
    {
        if (!fn) return;
        Providers().emplace_back(name, fn);
        WLOG_INFO("host: + provider hook '%s'", name ? name : "(unnamed)");
    }

    /**
     * @brief Appends an exists hook to the list, ignoring a null callback.
     * @param name  display name for the hook
     * @param fn    exists callback
     */
    void RegisterExists(const char* name, ExistsFn fn)
    {
        if (!fn) return;
        Existers().emplace_back(name, fn);
        WLOG_INFO("host: + exists hook '%s'", name ? name : "(unnamed)");
    }

    /**
     * @brief Appends a served observer hook to the list, ignoring a null callback.
     * @param name  display name for the hook
     * @param fn    served callback
     */
    void RegisterServed(const char* name, ServedFn fn)
    {
        if (!fn) return;
        Serveds().emplace_back(name, fn);
        WLOG_INFO("host: + served hook '%s'", name ? name : "(unnamed)");
    }

    /**
     * @brief Appends a FileDataID resolver to the list, ignoring a null callback.
     * @param name  display name for the resolver
     * @param fn    resolver callback
     */
    void RegisterResolver(const char* name, ResolveFn fn)
    {
        if (!fn) return;
        Resolvers().emplace_back(name, fn);
        WLOG_INFO("host: + resolver '%s'", name ? name : "(unnamed)");
    }

    /**
     * @brief Runs each provider hook in order until one supplies the bytes.
     * @param name  file name requested
     * @param out   receives the supplied bytes
     * @return true if a provider claimed the file
     */
    bool Provide(std::string_view name, std::vector<uint8_t>& out)
    {
        for (const auto& h : Providers())
            if (SafeProvide(h, name, out)) return true;
        return false;
    }

    /**
     * @brief Runs each transform hook in order until one reshapes the raw bytes.
     * @param name  file name being served
     * @param raw   raw archive bytes
     * @param out   receives the reshaped bytes
     * @return true if a transform claimed the file
     */
    bool Transform(std::string_view name, std::span<const uint8_t> raw, std::vector<uint8_t>& out)
    {
        for (const auto& h : Transforms())
            if (SafeTransform(h, name, raw, out)) return true;
        return false;
    }

    /**
     * @brief Runs each exists hook in order until one reports the file present.
     * @param name  file name queried
     * @return true if any hook reports the file present
     */
    bool Exists(std::string_view name)
    {
        for (const auto& h : Existers())
            if (SafeExists(h, name)) return true;
        return false;
    }

    /**
     * @brief Fires every served observer hook with the bytes handed to the client.
     * @param name   file name served
     * @param bytes  the bytes handed to the client
     */
    void NotifyServed(std::string_view name, std::span<const uint8_t> bytes)
    {
        for (const auto& h : Serveds())
            SafeServed(h, name, bytes);
    }

    /**
     * @brief Runs each resolver in order until one maps the FileDataID to a path.
     * @param fileDataId  the id to resolve
     * @param outPath     receives the path when resolved
     * @return true if a resolver claimed the id
     */
    bool ResolveFdid(uint32_t fileDataId, std::string& outPath)
    {
        for (const auto& h : Resolvers())
            if (SafeResolve(h, fileDataId, outPath)) return true;
        return false;
    }

    void WarmResolvers()
    {
        for (const auto& h : Resolvers())
        {
            std::string ignored;
            SafeResolve(h, 0, ignored);
        }
    }

    /**
     * @brief Returns the total number of registered hooks across all hook points.
     * @return registered hook count
     */
    uint32_t HandlerCount()
    {
        return static_cast<uint32_t>(
            Transforms().size() + Providers().size() + Existers().size() + Serveds().size() + Resolvers().size());
    }

    /** @brief Logs the registered hooks to the host log, grouped by hook point. */
    void LogRegisteredHandlers()
    {
        WLOG_INFO(
            "host: %u hook(s) registered (transform=%zu provider=%zu exists=%zu served=%zu resolver=%zu)",
            HandlerCount(), Transforms().size(), Providers().size(), Existers().size(), Serveds().size(), Resolvers().size());
        for (const auto& h : Transforms()) WLOG_INFO("host:   transform <- %s", h.name ? h.name : "(unnamed)");
        for (const auto& h : Providers())  WLOG_INFO("host:   provider  <- %s", h.name ? h.name : "(unnamed)");
        for (const auto& h : Existers())   WLOG_INFO("host:   exists    <- %s", h.name ? h.name : "(unnamed)");
        for (const auto& h : Serveds())    WLOG_INFO("host:   served    <- %s", h.name ? h.name : "(unnamed)");
        for (const auto& h : Resolvers())  WLOG_INFO("host:   resolver  <- %s", h.name ? h.name : "(unnamed)");
    }

    /** @brief Logs and resets the current per-hook profiling window. */
    void LogAndResetHandlerProfile()
    {
        if (!ProfilingEnabled()) return;
        const uint64_t frequency = PerformanceFrequency();
        LogAndResetProfiles("provider", Providers(), frequency);
        LogAndResetProfiles("transform", Transforms(), frequency);
        LogAndResetProfiles("exists", Existers(), frequency);
        LogAndResetProfiles("served", Serveds(), frequency);
        LogAndResetProfiles("resolver", Resolvers(), frequency);
    }
}
