// DLL IPC client: launch + connect to the asset host, run file ops over the shared-memory mailbox.
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

// Client side of the host shared-memory mailbox: sends a FlexBuffers request, reads the response.
// Thread-safe. Returns empty/false when the host is absent (the caller falls back to native I/O).
namespace wxl::runtime::ipc
{
    /**
     * @brief Launches the asset host if not already running.
     *
     * Fires OnBeforeHostLaunch first; a subscriber can cancel the auto-launch. Non-blocking.
     */
    void EnsureHostRunning();

    /**
     * @brief Blocks until the host mailbox exists or the timeout elapses.
     * @param timeoutMs  maximum wait in milliseconds.
     * @return true if the host is ready.
     */
    bool WaitForHost(uint32_t timeoutMs);

    /**
     * @brief Opens or reopens the host mailbox.
     * @return true if connected.
     */
    bool Connect();

    /**
     * @brief Reports whether the host mailbox is currently connected.
     * @return true while connected.
     */
    bool IsConnected();

    /**
     * @brief Result of a host file open.
     *
     * Three outcomes, distinguished so callers never confuse a transport failure with a real absence:
     *  - ok == true: served. id==0 -> inline, bytes in inlineData; id!=0 -> a shared section the client maps
     *    with MapBlob and reads directly (FileReadChunk is the fallback when mapping fails).
     *  - ok == false && hostMiss == true: the host answered and reported the file absent (a real, cacheable
     *    miss -> native fallback is correct).
     *  - ok == false && hostMiss == false: no usable answer (timeout / desync / bad request). Transient: fall
     *    back to native for THIS open only, and do NOT cache it -- the next open retries the host.
     */
    struct FileOpenResult { bool ok; bool hostMiss; uint32_t id; uint32_t size; std::vector<uint8_t> inlineData; };

    /**
     * @brief Opens a file from the host archive set.
     * @param name   archive-relative file name.
     * @param flags  native open flags.
     * @return the open result.
     */
    FileOpenResult FileOpen(const std::string& name, uint32_t flags);

    /**
     * @brief Maps the host blob section for an id read-only.
     * @param id         host blob id.
     * @param size       section size to map.
     * @param outView    receives the mapped view.
     * @param outHandle  receives the section handle.
     * @return true on success.
     */
    bool MapBlob(uint32_t id, uint32_t size, void*& outView, void*& outHandle);

    /**
     * @brief Releases a mapping from MapBlob (null-safe).
     * @param view    mapped view.
     * @param handle  section handle.
     */
    void UnmapBlob(void* view, void* handle);

    /**
     * @brief Reads up to cap bytes at an offset into dst in one round trip (capped at kFileChunkMax).
     * @param id   host file id.
     * @param off  byte offset to read from.
     * @param dst  destination buffer.
     * @param cap  maximum bytes to copy.
     * @return bytes copied.
     */
    uint32_t FileReadChunk(uint32_t id, uint32_t off, void* dst, uint32_t cap);

    /**
     * @brief Releases a host file id.
     * @param id  host file id.
     */
    void FileClose(uint32_t id);

    /**
     * @brief Tests whether a file exists in the host archive set.
     * @param name  archive-relative file name.
     * @return true if the host reports the file present.
     */
    bool FileExists(const std::string& name);
}
