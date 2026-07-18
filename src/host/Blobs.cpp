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

#include "Blobs.hpp"

#include "ipc/Protocol.hpp"

#include <windows.h>
#include <cstring>
#include <mutex>
#include <unordered_map>

using wxl::ipc::BlobName;

namespace wxl::host::blobs
{
    namespace
    {
        /** @brief Holds a shared section serving one file's bytes zero-copy to the client. */
        struct Blob { HANDLE section; void* view; uint32_t size; uint32_t refs; std::string name; };

        // g_mutex protects every field below: the two maps, the byte/ref gauges, and the id counter.
        std::mutex g_mutex;
        std::unordered_map<uint32_t, Blob> g_blobs;
        std::unordered_map<std::string, uint32_t> g_blobByName;
        uint64_t g_blobBytes = 0;
        uint64_t g_blobRefs = 0;
        uint32_t g_nextHandle = 0;
    }

    uint32_t Store(const std::string& name, const std::vector<uint8_t>& bytes, bool& reused)
    {
        reused = false;
        uint32_t size = static_cast<uint32_t>(bytes.size());

        /**
         * @brief Reuses (and refs) an already-stored section for name if one matches.
         * @return the existing id, or 0 when no reusable section exists. Caller holds g_mutex.
         */
        const auto tryReuseLocked = [&]() -> uint32_t
        {
            auto existingName = g_blobByName.find(name);
            if (existingName == g_blobByName.end()) return 0;
            auto existingBlob = g_blobs.find(existingName->second);
            if (existingBlob != g_blobs.end() && existingBlob->second.size == size && existingBlob->second.view)
            {
                ++existingBlob->second.refs;
                ++g_blobRefs;
                reused = true;
                return existingName->second;
            }
            g_blobByName.erase(existingName);
            return 0;
        };

        uint32_t id = 0;
        {
            std::lock_guard<std::mutex> hl(g_mutex);
            if (const uint32_t existing = tryReuseLocked()) return existing;
            id = ++g_nextHandle;
            if (id == 0) id = ++g_nextHandle; // ids must be nonzero
        }

        // The section create/map and the (multi-MB) copy run outside the lock: this is the hottest
        // large-file path and every open/read/close otherwise serializes behind the copy.
        char nm[64];
        BlobName(nm, sizeof(nm), id);
        HANDLE h = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, size ? size : 1, nm);
        if (!h) return 0;
        void* v = MapViewOfFile(h, FILE_MAP_WRITE, 0, 0, size);
        if (!v) { CloseHandle(h); return 0; }
        if (size) memcpy(v, bytes.data(), size);

        std::lock_guard<std::mutex> hl(g_mutex);
        if (const uint32_t existing = tryReuseLocked())
        {
            // Another worker stored the same name while we copied; keep its section so the
            // one-name-one-section dedup holds, and discard ours.
            UnmapViewOfFile(v);
            CloseHandle(h);
            return existing;
        }
        g_blobs.emplace(id, Blob{ h, v, size, 1, name });
        g_blobByName.emplace(name, id);
        g_blobBytes += size;
        ++g_blobRefs;
        return id;
    }

    bool Read(uint32_t id, uint32_t off, uint32_t len, std::vector<uint8_t>& out)
    {
        std::lock_guard<std::mutex> hl(g_mutex);
        auto it = g_blobs.find(id);
        if (it == g_blobs.end()) return false;

        const Blob& b = it->second;
        if (off < b.size && b.view)
        {
            uint32_t avail = b.size - off;
            uint32_t n = len < avail ? len : avail;
            const uint8_t* src = static_cast<const uint8_t*>(b.view) + off;
            out.assign(src, src + n);
        }
        return true;
    }

    void Close(uint32_t id)
    {
        std::lock_guard<std::mutex> hl(g_mutex);
        auto it = g_blobs.find(id);
        if (it == g_blobs.end()) return;

        if (it->second.refs > 1)
        {
            --it->second.refs;
            --g_blobRefs;
            return;
        }
        auto nameIt = g_blobByName.find(it->second.name);
        if (nameIt != g_blobByName.end() && nameIt->second == id)
            g_blobByName.erase(nameIt);
        g_blobBytes -= it->second.size;
        --g_blobRefs;
        if (it->second.view)    UnmapViewOfFile(it->second.view);
        if (it->second.section) CloseHandle(it->second.section);
        g_blobs.erase(it);
    }

    void SnapshotGauges(size_t& count, uint64_t& bytes, uint64_t& refs)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        count = g_blobs.size();
        bytes = g_blobBytes;
        refs = g_blobRefs;
    }
}
