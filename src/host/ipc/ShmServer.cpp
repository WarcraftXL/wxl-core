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

#include "ipc/ShmServer.hpp"

#include <windows.h>

using namespace wxl::ipc;

namespace
{
    // Shared window and per-channel event pairs. The host creates these; the client opens them. Set once
    // in Create and read-only afterwards: each worker touches only its own channel's payload, and the
    // control words it writes (respLen/respSeq) are single-writer per channel.
    HANDLE   g_shm = nullptr;
    uint8_t* g_base = nullptr;
    HANDLE   g_reqEv[kChannels]  = {};
    HANDLE   g_respEv[kChannels] = {};
}

namespace wxl::host::ipc
{
    /**
     * @brief Creates and maps the shared window, stamps each channel header, and creates the events.
     * @return true on success
     */
    bool Create()
    {
        g_shm = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, kShmSize, kShmName);
        if (!g_shm) return false;
        g_base = static_cast<uint8_t*>(MapViewOfFile(g_shm, FILE_MAP_ALL_ACCESS, 0, 0, kShmSize));
        if (!g_base) { CloseHandle(g_shm); g_shm = nullptr; return false; }

        // Zero every channel header and stamp magic/version so the client's connect check passes.
        for (uint32_t i = 0; i < kChannels; ++i)
        {
            ControlHeader* hdr = ChannelHeader(g_base, i);
            ZeroMemory(hdr, sizeof(*hdr));
            hdr->magic = kMagic;
            hdr->version = kVersion;
        }

        // Auto-reset events, initially non-signaled. The client opens the same names.
        for (uint32_t i = 0; i < kChannels; ++i)
        {
            char rn[64], sn[64];
            ReqEventName(rn, sizeof(rn), i);
            RespEventName(sn, sizeof(sn), i);
            g_reqEv[i]  = CreateEventA(nullptr, FALSE, FALSE, rn);
            g_respEv[i] = CreateEventA(nullptr, FALSE, FALSE, sn);
            if (!g_reqEv[i] || !g_respEv[i]) return false;
        }
        return true;
    }

    /**
     * @brief Blocks on channel `i`'s request event and copies the request payload out.
     * @param i       channel index
     * @param reqOut  receives the request payload bytes
     * @return true if a request was read
     */
    bool WaitRequest(uint32_t i, std::vector<uint8_t>& reqOut)
    {
        if (i >= kChannels) return false;
        if (WaitForSingleObject(g_reqEv[i], INFINITE) != WAIT_OBJECT_0) return false;

        const ControlHeader* hdr = ChannelHeader(g_base, i);
        const uint8_t* payload = ChannelPayload(g_base, i);
        uint32_t n = hdr->reqLen;
        if (n > kChannelPayload) n = 0; // malformed: hand the worker an empty request
        reqOut.assign(payload, payload + n);
        return true;
    }

    /**
     * @brief Copies the response payload into channel `i`, marks it complete, and signals the client.
     * @param i     channel index
     * @param resp  response payload bytes
     * @return true if a nonzero-length response was written
     */
    bool PostResponse(uint32_t i, std::span<const uint8_t> resp)
    {
        if (i >= kChannels) return false;

        ControlHeader* hdr = ChannelHeader(g_base, i);
        uint8_t* payload = ChannelPayload(g_base, i);
        uint32_t n = static_cast<uint32_t>(resp.size());
        if (n > kChannelPayload) n = 0; // never overrun the window; signal a zero-length response
        if (n) memcpy(payload, resp.data(), n);
        hdr->respLen = n;
        hdr->respSeq = hdr->reqSeq; // marks the response complete for this request
        SetEvent(g_respEv[i]);
        return n != 0;
    }
}
