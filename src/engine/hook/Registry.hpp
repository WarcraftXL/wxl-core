// Feature installer registry: features self-register before main, the bootstrap unrolls them.
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

/// A registry of feature installers. Each feature drops one static registrar (via
/// WXL_REGISTER_FEATURE) that records its name, its compile-time toggle, an install callback and the
/// bootstrap phase it belongs to; the runtime bootstrap later calls InstallRegisteredFeatures(phase)
/// to run the enabled ones of that phase in registration order. The backing store is a
/// function-local static, so registration works safely before main() regardless of
/// static-initialisation order across translation units.
namespace wxl::hook
{
    /**
     * @brief When a feature's installer runs, relative to the boot sequence.
     *
     * A single feature may register more than one installer across phases (e.g. streaming reserves
     * its early disk-queue detours at Boot and its drain/chunk detours at Normal). All installers of
     * one phase run together; the bootstrap enables the hook batch between phases as needed.
     */
    enum class Phase
    {
        Boot,      ///< loader thread, inside DllMain, before the client's own startup proceeds
        Normal,    ///< main thread, after the graphics device exists, before the hook batch is enabled
        PostEnable ///< main thread, after the hook batch is enabled (for steps that need live detours)
    };

    /**
     * @brief Records one feature installer. Called by the static registrars before main.
     * @param name     label used for logging.
     * @param enabled  compile-time toggle from wxl::features; a false entry is never installed.
     * @param install  installer; returns false to signal a fatal install failure.
     * @param phase    bootstrap phase this installer belongs to.
     */
    void RegisterFeature(const char* name, bool enabled, bool (*install)(), Phase phase = Phase::Normal);

    /**
     * @brief Runs every enabled installer of one phase once, in registration order.
     * @param phase  the phase whose installers to run.
     * @return the number of features successfully installed.
     */
    int InstallRegisteredFeatures(Phase phase = Phase::Normal);

    /// Ctor-side shim: constructing one records a feature with the registry. Instantiate a single
    /// static of these per feature through WXL_REGISTER_FEATURE / WXL_REGISTER_FEATURE_PHASED.
    struct FeatureRegistrar
    {
        FeatureRegistrar(const char* name, bool enabled, bool (*install)(), Phase phase = Phase::Normal)
        {
            RegisterFeature(name, enabled, install, phase);
        }
    };
}

#define WXL_HOOK_CONCAT_(a, b) a##b
#define WXL_HOOK_CONCAT(a, b) WXL_HOOK_CONCAT_(a, b)

/**
 * @brief Registers a feature installer at static-init time, run in the Normal phase.
 * @param name    string label for logging.
 * @param toggle  a wxl::features::k* constexpr bool; when false the feature never installs.
 * @param fn      bool(*)() installer.
 */
#define WXL_REGISTER_FEATURE(name, toggle, fn)                                 \
    namespace                                                                  \
    {                                                                          \
        const ::wxl::hook::FeatureRegistrar                                    \
            WXL_HOOK_CONCAT(g_wxlFeatureRegistrar_, __LINE__){name, toggle, fn}; \
    }

/**
 * @brief Registers a feature installer at static-init time in an explicit phase.
 * @param name    string label for logging.
 * @param toggle  a wxl::features::k* constexpr bool; when false the feature never installs.
 * @param fn      bool(*)() installer.
 * @param phase   a ::wxl::hook::Phase value.
 */
#define WXL_REGISTER_FEATURE_PHASED(name, toggle, fn, phase)                          \
    namespace                                                                         \
    {                                                                                 \
        const ::wxl::hook::FeatureRegistrar                                           \
            WXL_HOOK_CONCAT(g_wxlFeatureRegistrar_, __LINE__){name, toggle, fn, phase}; \
    }
