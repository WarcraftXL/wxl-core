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
     * @brief Creates and maps the shared window and per-channel events.
     * @return true on success
     */
    bool Create();

    /**
     * @brief Blocks until channel `i` has a request and returns its payload bytes.
     * @param i       channel index
     * @param reqOut  receives the request payload bytes
     * @return true if a request was read
     */
    bool WaitRequest(uint32_t i, std::vector<uint8_t>& reqOut);

    /**
     * @brief Writes the response payload to channel `i` and signals the client.
     * @param i     channel index
     * @param resp  response payload bytes
     * @return true if a nonzero-length response was written
     */
    bool PostResponse(uint32_t i, std::span<const uint8_t> resp);
}
