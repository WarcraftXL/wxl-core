// Host hook surface: registers hooks and runs them from the serve pipeline.
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

#include "core/Logger.hpp"

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
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace wxl::host
{
    namespace
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

        /** @brief Holds a hook profile's short update/snapshot critical section. */
        class HookProfileGuard
        {
        public:
            explicit HookProfileGuard(HookProfile& profile) : profile_(profile)
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

            ~HookProfileGuard()
            {
                profile_.guard.clear(std::memory_order_release);
            }

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
            wxl::core::log::Printf("host: %s hook '%s' crashed while handling '%.*s'; skipping",
                phase ? phase : "unknown",
                hookName ? hookName : "(unnamed)",
                static_cast<int>(n),
                name.data() ? name.data() : "");
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
                wxl::core::log::Printf("host: resolver '%s' crashed while handling FileDataID %u; skipping",
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
                wxl::core::log::Printf("host: resolver '%s' crashed while handling FileDataID %u; skipping",
                    h.name ? h.name : "(unnamed)", fileDataId);
                FinishProfile(h, profiling, started, false, true, false);
                return false;
            }
        }
#endif

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
            wxl::core::log::Printf(
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
        static const bool enabled = []
        {
            const char* raw = std::getenv("WXL_HOST_PROFILE");
            if (!raw || !*raw) return true;
            return !(*raw == '0' || *raw == 'n' || *raw == 'N' || *raw == 'f' || *raw == 'F');
        }();
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
        wxl::core::log::Printf("host: + transform hook '%s'", name ? name : "(unnamed)");
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
        wxl::core::log::Printf("host: + provider hook '%s'", name ? name : "(unnamed)");
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
        wxl::core::log::Printf("host: + exists hook '%s'", name ? name : "(unnamed)");
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
        wxl::core::log::Printf("host: + served hook '%s'", name ? name : "(unnamed)");
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
        wxl::core::log::Printf("host: + resolver '%s'", name ? name : "(unnamed)");
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

    /** @brief Returns the storage holding the client data root. */
    std::string& ClientRootRef() { static std::string s; return s; }
    /**
     * @brief Stores the client data root.
     * @param root  client data root path
     */
    void SetClientRoot(std::string_view root) { ClientRootRef().assign(root); }
    /** @brief Returns the client data root. */
    std::string ClientRoot() { return ClientRootRef(); }

    namespace
    {
        /** @brief Lowercases and swaps '/' for '\\' so two spellings of the same path compare equal. */
        std::string NormalizeTexturePath(std::string_view path)
        {
            std::string s(path);
            for (char& c : s)
            {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (c == '/') c = '\\';
            }
            return s;
        }

        /** @brief Guards ModernTextureSet(). */
        std::mutex& ModernTextureMutex() { static std::mutex m; return m; }
        /** @brief Returns the process-wide set of modern-sourced texture paths. */
        std::unordered_set<std::string>& ModernTextureSet() { static std::unordered_set<std::string> s; return s; }
    }

    void MarkModernTexture(std::string_view path)
    {
        std::lock_guard<std::mutex> lock(ModernTextureMutex());
        ModernTextureSet().insert(NormalizeTexturePath(path));
    }

    bool IsModernTexture(std::string_view path)
    {
        std::lock_guard<std::mutex> lock(ModernTextureMutex());
        const auto& set = ModernTextureSet();
        return set.find(NormalizeTexturePath(path)) != set.end();
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
        wxl::core::log::Printf(
            "host: %u hook(s) registered (transform=%zu provider=%zu exists=%zu served=%zu resolver=%zu)",
            HandlerCount(), Transforms().size(), Providers().size(), Existers().size(), Serveds().size(), Resolvers().size());
        for (const auto& h : Transforms()) wxl::core::log::Printf("host:   transform <- %s", h.name ? h.name : "(unnamed)");
        for (const auto& h : Providers())  wxl::core::log::Printf("host:   provider  <- %s", h.name ? h.name : "(unnamed)");
        for (const auto& h : Existers())   wxl::core::log::Printf("host:   exists    <- %s", h.name ? h.name : "(unnamed)");
        for (const auto& h : Serveds())    wxl::core::log::Printf("host:   served    <- %s", h.name ? h.name : "(unnamed)");
        for (const auto& h : Resolvers())  wxl::core::log::Printf("host:   resolver  <- %s", h.name ? h.name : "(unnamed)");
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
