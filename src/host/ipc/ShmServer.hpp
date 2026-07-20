// ShmServer: the host side of the shared-memory transport (create window + per-channel events).
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
#include <vector>

#include "ipc/Protocol.hpp"

// Host side of the shared-memory transport: creates the window and per-channel events, then reads
// requests and writes responses. The host creates the objects; the client opens them.
namespace wxl::host::ipc
{
    /**
     * @brief Creates and maps the shared window and per-channel events, sizing the channel count from
     *        the machine's hardware_concurrency (clamped to [kMinChannels, kMaxChannels]).
     * @param sessionPid  client process id that owns this host session (names the OS objects)
     * @return true on success
     */
    bool Create(uint32_t sessionPid);

    /** @brief Returns the channel count chosen by Create() (0 before it succeeds). */
    uint32_t ChannelCount();

    /** @brief Returns the session PID passed to Create() (0 before it succeeds). */
    uint32_t SessionPid();

    /**
     * @brief Blocks until any channel has a request; a pool of worker threads (fewer than the channel
     *        count) all wait on the full channel set, so whichever thread is free picks up the next
     *        signaled channel instead of each channel being tied to one dedicated thread.
     * @param channelOut  receives the channel index that had the request
     * @param seqOut      receives the request sequence captured with the payload
     * @param reqOut      receives the request payload bytes
     * @return true if a request was read
     */
    bool WaitAnyRequest(uint32_t& channelOut, uint32_t& seqOut, std::vector<uint8_t>& reqOut);

    /**
     * @brief Writes the response payload to channel `i` and signals the client.
     * @param i     channel index
     * @param seq   request sequence this response belongs to (echoed back so the client can match it)
     * @param resp  response payload bytes
     * @return true if a nonzero-length response was written
     */
    bool PostResponse(uint32_t i, uint32_t seq, std::span<const uint8_t> resp);
}
