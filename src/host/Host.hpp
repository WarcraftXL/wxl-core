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

    // --- FileDataID resolution (cold boundary) ---

    /**
     * @brief Resolves a FileDataID to an archive-internal path; returns true when resolved.
     * @param fileDataId  the id to resolve
     * @param outPath     receives the path when resolved
     * @return true if this resolver mapped the id
     */
    using ResolveFn = bool (*)(uint32_t fileDataId, std::string& outPath);

    /**
     * @brief Registers a FileDataID resolver under a display name. A module owns the resolution table (e.g.
     *        the DB2 path tables) and registers here; the asset transforms call ResolveFdid.
     * @param name  display name for the resolver (logging)
     * @param fn    resolver callback
     */
    void RegisterResolver(const char* name, ResolveFn fn);

    /**
     * @brief Runs the registered resolvers for `fileDataId`; returns true when one maps it. With no
     *        registrant resolution fails and a transform falls back to its placeholder.
     * @param fileDataId  the id to resolve
     * @param outPath     receives the path when resolved
     * @return true if a resolver claimed the id
     */
    bool ResolveFdid(uint32_t fileDataId, std::string& outPath);

    /**
     * @brief Invokes each resolver once with a sentinel id so lazy path tables load before IPC requests.
     *
     * The sentinel is not expected to resolve; this only moves cold initialization off the client wait path.
     */
    void WarmResolvers();

    // --- host environment (set by the host at startup; read by modules that read the archives themselves,
    //     e.g. a resolver mounting its own MpqStore to load DB2 path tables) ---

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

    // --- texture provenance (cold boundary; a resolver call site marks a texture path it just resolved for
    //     a modern M2/WMO/ADT source, so a byte-transform that must not touch native content -- e.g. the BLP
    //     size cap -- can scope itself to modern-sourced textures instead of every texture the client opens)
    //     ---

    /**
     * @brief Marks a texture path as referenced by a modern (non-native) source. Idempotent.
     * @param path  texture path, as returned by a FileDataID resolver
     */
    void MarkModernTexture(std::string_view path);

    /**
     * @brief Reports whether a texture path was marked as referenced by a modern source.
     * @param path  texture path queried, in whatever form the caller has it (matched case/separator
     *              insensitively against every path passed to MarkModernTexture)
     * @return true if the path was marked
     */
    bool IsModernTexture(std::string_view path);

    // --- registry introspection ---

    /**
     * @brief Returns the total number of registered hooks across all hook points.
     * @return registered hook count
     */
    uint32_t HandlerCount();

    /** @brief Logs the registered hooks to the host log, grouped by hook point. */
    void LogRegisteredHandlers();

    /**
     * @brief Reports whether per-hook timing is enabled by WXL_HOST_PROFILE (enabled by default).
     * @return false only when the variable begins with 0, n, N, f, or F
     */
    bool ProfilingEnabled();

    /** @brief Logs each active hook's profiling window and atomically resets its counters. */
    void LogAndResetHandlerProfile();
}
