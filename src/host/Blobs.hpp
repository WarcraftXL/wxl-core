// Shared-section blob store: serves large files zero-copy through named shared memory.
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
#include <string>
#include <vector>

// Large files are served zero-copy: the host copies the bytes once into a named shared section and keeps
// it alive until the client closes the id. Repeated opens of the same name share one section under a
// refcount. Every function here is safe to call from any worker; a single internal mutex guards the store.
namespace wxl::host::blobs
{
    /**
     * @brief Copies whole bytes into a named shared section and returns its nonzero id.
     *
     * Repeated opens of the same archive name share one section with a refcount. This is especially useful
     * for looping sounds and hot city textures, which otherwise create and map a fresh host blob on every
     * open even when the payload is identical.
     * @param name   file name whose bytes are being served
     * @param bytes  file bytes to place in the section
     * @param reused receives true when an existing named section was retained
     * @return the nonzero blob id, or 0 if the section or view could not be created
     */
    uint32_t Store(const std::string& name, const std::vector<uint8_t>& bytes, bool& reused);

    /**
     * @brief Copies a byte range out of blob `id` for a client that cannot map the section directly.
     * @param id   blob id to read
     * @param off  start offset
     * @param len  maximum number of bytes to copy (already clamped by the caller to the protocol chunk cap)
     * @param out  receives the bytes copied (clamped to the section end)
     * @return true if the id names a live blob
     */
    bool Read(uint32_t id, uint32_t off, uint32_t len, std::vector<uint8_t>& out);

    /**
     * @brief Releases one reference to blob `id`, unmapping and closing its section when the last ref drops.
     * @param id  blob id to release
     */
    void Close(uint32_t id);

    /**
     * @brief Samples live shared-section memory for a periodic profile report.
     * @param count  receives the number of live blobs
     * @param bytes  receives the total resident blob bytes
     * @param refs   receives the total outstanding client references
     */
    void SnapshotGauges(size_t& count, uint64_t& bytes, uint64_t& refs);
}
