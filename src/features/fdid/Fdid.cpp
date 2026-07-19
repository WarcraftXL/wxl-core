// FileDataID resolver -- implementation. Asks the 64-bit host (the DB2 authority) over IPC + caches.
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

#include "features/fdid/Fdid.hpp"

#include "engine/storage/ShmClient.hpp"

#include <mutex>
#include <string>
#include <unordered_map>

namespace wxl::fdid
{
    namespace
    {
        namespace ipc = wxl::runtime::ipc;

        std::mutex                                g_mutex;
        // fdid -> resolved path; an empty string is a cached, host-confirmed miss (so a texture shared
        // by hundreds of tiles is only ever asked once). Node pointers are stable across rehash, so
        // returning c_str() after the lock drops is safe. Never erased -> pointers live for the process.
        std::unordered_map<uint32_t, std::string> g_cache;

        // Textures and models share one FileDataID space, and the host resolver searches both path
        // tables, so a single query serves either kind.
        const char* Resolve(uint32_t fdid)
        {
            if (fdid == 0)
                return nullptr;

            std::lock_guard<std::mutex> lock(g_mutex);
            auto it = g_cache.find(fdid);
            if (it == g_cache.end())
            {
                std::string path;
                const bool ok = ipc::ResolveFdid(fdid, path);
                // Cache a positive answer always; cache a miss only when the host actually answered
                // (connected) -- a transport failure while disconnected must stay retryable, not poison
                // the entry into a permanent miss for the session.
                if (!ok && !ipc::IsConnected())
                    return nullptr;
                it = g_cache.emplace(fdid, std::move(path)).first;
            }
            return it->second.empty() ? nullptr : it->second.c_str();
        }
    } // namespace

    const char* ResolveTexture(uint32_t fileDataId) { return Resolve(fileDataId); }
    const char* ResolveModel(uint32_t fileDataId)   { return Resolve(fileDataId); }
}
