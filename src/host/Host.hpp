// Host hook surface: the serve pipeline fires these hooks; module host faces register into them.
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
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Host hook surface. WarcraftXLHost.exe (64-bit) owns the archive set and the IPC transport and is
// format-blind. A module contributes a host face under scripts/<module>/host/ that, from a file-scope
// registrar, hooks the serve pipeline here at startup. This header lists the points a module can plug into.
//
// Each hook below is fired by the serve pipeline on the matching request. With no registrant the fire is a
// no-op and the host serves raw archive bytes. Handlers run in registration order; the first that claims a
// request wins.
namespace wxl::host
{
    /**
     * @brief Reshapes raw archive bytes for `name`; returns true when the hook produced output.
     * @param name  archive-internal file name being served
     * @param raw   raw bytes read from the archive
     * @param out   receives the reshaped bytes when the hook claims the file
     * @return true if this hook produced `out`, false to defer
     */
    using TransformFn = bool (*)(std::string_view name, std::span<const uint8_t> raw, std::vector<uint8_t>& out);

    /**
     * @brief Registers a transform hook under a display name.
     * @param name  display name for the hook (logging)
     * @param fn    transform callback
     */
    void RegisterTransform(const char* name, TransformFn fn);

    /**
     * @brief Supplies the whole bytes for `name` without reading the archives; returns true when claimed.
     * @param name  file name requested
     * @param out   receives the supplied bytes when the hook claims the file
     * @return true if this hook supplied `out`, false to defer to the archive read
     */
    using ProvideFn = bool (*)(std::string_view name, std::vector<uint8_t>& out);

    /**
     * @brief Registers a provider hook under a display name.
     * @param name  display name for the hook (logging)
     * @param fn    provider callback
     */
    void RegisterProvider(const char* name, ProvideFn fn);

    /**
     * @brief Reports whether `name` exists according to this hook.
     * @param name  file name queried
     * @return true if the hook can serve `name`
     */
    using ExistsFn = bool (*)(std::string_view name);

    /**
     * @brief Registers an exists hook under a display name.
     * @param name  display name for the hook (logging)
     * @param fn    exists callback
     */
    void RegisterExists(const char* name, ExistsFn fn);

    /**
     * @brief Observes the final bytes served for a client open, after Provide and Transform, without altering them.
     * @param name   file name served
     * @param bytes  the bytes handed to the client
     */
    using ServedFn = void (*)(std::string_view name, std::span<const uint8_t> bytes);

    /**
     * @brief Registers a served observer hook under a display name.
     * @param name  display name for the hook (logging)
     * @param fn    served callback
     */
    void RegisterServed(const char* name, ServedFn fn);

    // --- pipeline entry points (called by the host serve loop, NOT by modules) ---

    /**
     * @brief Runs the provider hooks for `name`; returns true when one supplies the bytes.
     * @param name  file name requested
     * @param out   receives the supplied bytes
     * @return true if a provider claimed the file
     */
    bool Provide(std::string_view name, std::vector<uint8_t>& out);

    /**
     * @brief Runs the transform hooks for `name`; returns true when one reshapes the raw bytes.
     * @param name  file name being served
     * @param raw   raw archive bytes
     * @param out   receives the reshaped bytes
     * @return true if a transform claimed the file
     */
    bool Transform(std::string_view name, std::span<const uint8_t> raw, std::vector<uint8_t>& out);

    /**
     * @brief Runs the exists hooks for `name`.
     * @param name  file name queried
     * @return true if any exists hook reports the file present
     */
    bool Exists(std::string_view name);

    /**
     * @brief Fires the served observer hooks for the bytes handed to the client.
     * @param name   file name served
     * @param bytes  the bytes handed to the client
     */
    void NotifyServed(std::string_view name, std::span<const uint8_t> bytes);

    // --- host environment (set by the host at startup; read by modules that read the archives themselves,
    //     e.g. the prefetch pool mounting its own per-thread MpqStore) ---

    /**
     * @brief Stores the client data root for modules that read the archives themselves.
     * @param root  client data root path
     */
    void SetClientRoot(std::string_view root);

    /**
     * @brief Returns the client data root set at startup.
     * @return client data root path
     */
    std::string ClientRoot();

    // --- registry introspection ---

    /**
     * @brief Returns the total number of registered hooks across all hook points.
     * @return registered hook count
     */
    uint32_t HandlerCount();

    /** @brief Logs the registered hooks to the host log, grouped by hook point. */
    void LogRegisteredHandlers();
}
