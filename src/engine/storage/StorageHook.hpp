// Storage I/O hook: launch the host, then forward archive file opens to it (asset-agnostic).
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
#include <vector>

namespace wxl::runtime::storage
{
    /**
     * @brief Arms the archive-mount guard that drops the host-owned loose directories at mount time.
     *
     * Keeps the client from indexing the modern data tree into its 32-bit address space. Must run
     * before the client builds its archive set, so call it from the DLL entry on the loader thread
     * (not from the deferred main thread, which the client's startup races past). No host dependency.
     */
    void InstallArchiveGuard();

    /**
     * @brief Launches the asset host and hooks the client archive file-I/O primitives.
     *
     * A file the host serves is read from the host's bytes; everything else runs native. The hooks
     * are harmless with no host (every open falls through). Call once at startup, before EnableAll.
     */
    void Install();

    /**
     * @brief Callback type for client-side virtual file providers.
     * @param name  file name requested by the engine
     * @param out   receives the file bytes when the provider claims the name
     * @return true if this provider supplied bytes for name, false to pass through to the host
     */
    using ClientProvideFn = bool(*)(const char* name, std::vector<uint8_t>& out);

    /**
     * @brief Registers a client-side file provider.
     *
     * Providers are checked in TryServe before the host IPC round-trip. A provider that returns
     * true claims the file; the first claimant wins. Safe to call from a global constructor
     * (no hooking engine involved — just a function pointer stored in a list).
     * @param fn  provider callback to register
     */
    void RegisterClientProvider(ClientProvideFn fn);

    /**
     * @brief Filter for materialized host bytes before the native loader sees them.
     * @return Byte count to expose to native code; may trim trailing module side tables.
     */
    using ServeFilterFn = uint32_t(*)(const char* name, const uint8_t* buffer, uint32_t size);

    /** Registers a served-bytes filter; safe from a module global constructor. */
    void RegisterServeFilter(ServeFilterFn fn);
}
