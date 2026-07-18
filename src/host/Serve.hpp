// Serve subsystem: mounts the archives, creates the transport, and runs the request loop.
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

// The live serve subsystem: brings up the archive store, warmers and IPC transport, then runs the pooled
// request loop until the client closes. Decodes each request, dispatches to the produce pipeline and the
// blob store, and encodes the FlexBuffers response. The offline --provide dump path lives in main instead.
namespace wxl::host::serve
{
    /**
     * @brief Mounts the archives, starts the warmers and transport, and serves requests until the client exits.
     * @param clientRoot  client data root (parent of the host data folder)
     * @param clientPid   client process id to shadow, or 0 to run without a watcher
     * @return process exit code
     */
    int Run(const std::string& clientRoot, uint32_t clientPid);
}
