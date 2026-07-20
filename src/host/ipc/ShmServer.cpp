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

#include "core/Logger.hpp"

#include <windows.h>
#include <algorithm>
#include <thread>

using namespace wxl::ipc;

namespace
{
    // Shared window and per-channel event pairs. The host creates these; the client opens them. Set once
    // in Create and read-only afterwards: each worker touches only its own channel's payload, and the
    // control words it writes (respLen/respSeq) are single-writer per channel. Arrays are sized to the
    // safety bound kMaxChannels; only the first g_channelCount slots are ever populated or scanned.
    HANDLE   g_shm = nullptr;
    uint8_t* g_base = nullptr;
    uint32_t g_channelCount = 0;
    uint32_t g_sessionPid = 0;
    HANDLE   g_reqEv[kMaxChannels]  = {};
    HANDLE   g_respEv[kMaxChannels] = {};

    /**
     * @brief Picks the channel count from the machine's hardware_concurrency, clamped to a sane range.
     * @return channel count in [kMinChannels, kMaxChannels]
     */
    uint32_t ComputeChannelCount()
    {
        unsigned hw = std::thread::hardware_concurrency();
        if (hw == 0) hw = kMinChannels * 2; // undetectable: a conservative default
        return std::clamp<uint32_t>(hw, kMinChannels, kMaxChannels);
    }
}

namespace wxl::host::ipc
{
    /**
     * @brief Creates and maps the shared window, stamps each channel header, and creates the events.
     * @param sessionPid  client process id that owns this host session
     * @return true on success
     */
    bool Create(uint32_t sessionPid)
    {
        if (!sessionPid) return false;
        g_sessionPid = sessionPid;
        g_channelCount = ComputeChannelCount();
        const uint32_t shmSize = ShmSize(g_channelCount);

        char shmName[64];
        ShmName(shmName, sizeof(shmName), g_sessionPid);
        g_shm = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, shmSize, shmName);
        if (!g_shm) return false;
        g_base = static_cast<uint8_t*>(MapViewOfFile(g_shm, FILE_MAP_ALL_ACCESS, 0, 0, shmSize));
        if (!g_base) { CloseHandle(g_shm); g_shm = nullptr; return false; }

        // Zero every channel header, stamp magic/version so the client's connect check passes, and
        // record the channel count in channel 0's header so the client learns it rather than guessing.
        for (uint32_t i = 0; i < g_channelCount; ++i)
        {
            ControlHeader* hdr = ChannelHeader(g_base, i);
            ZeroMemory(hdr, sizeof(*hdr));
            hdr->magic = kMagic;
            hdr->version = kVersion;
            hdr->channelCount = g_channelCount;
        }

        // Auto-reset events, initially non-signaled. The client opens the same names.
        for (uint32_t i = 0; i < g_channelCount; ++i)
        {
            char rn[64], sn[64];
            ReqEventName(rn, sizeof(rn), g_sessionPid, i);
            RespEventName(sn, sizeof(sn), g_sessionPid, i);
            g_reqEv[i]  = CreateEventA(nullptr, FALSE, FALSE, rn);
            g_respEv[i] = CreateEventA(nullptr, FALSE, FALSE, sn);
            if (!g_reqEv[i] || !g_respEv[i]) return false;
        }

        wxl::core::log::Printf(
            "host: ipc window sized for %u channel(s) (sessionPid=%u, hardware_concurrency=%u)",
            g_channelCount, g_sessionPid, std::thread::hardware_concurrency());
        return true;
    }

    /** @brief Returns the channel count chosen by Create() (0 before it succeeds). */
    uint32_t ChannelCount() { return g_channelCount; }

    /** @brief Returns the session PID passed to Create() (0 before it succeeds). */
    uint32_t SessionPid() { return g_sessionPid; }

    /**
     * @brief Blocks until any channel signals a request, then copies that channel's sequence and payload out.
     *
     * Auto-reset events guarantee exactly one waiting thread is released per SetEvent, so when several
     * worker threads all call this concurrently, a burst of N signaled channels wakes exactly N threads
     * (one channel each) -- never a double-dispatch of the same channel.
     * @param channelOut  receives the channel index that had the request
     * @param seqOut      receives the request sequence captured with the payload
     * @param reqOut      receives the request payload bytes
     * @return true if a request was read
     */
    bool WaitAnyRequest(uint32_t& channelOut, uint32_t& seqOut, std::vector<uint8_t>& reqOut)
    {
        // A transient WAIT_FAILED must not retire the calling worker for the rest of the process
        // (returning false ends its loop): retry, and only give up after sustained failure.
        uint32_t failures = 0;
        DWORD rc;
        while ((rc = WaitForMultipleObjects(g_channelCount, g_reqEv, FALSE, INFINITE)) < WAIT_OBJECT_0
               || rc >= WAIT_OBJECT_0 + g_channelCount)
        {
            ++failures;
            if (failures == 1 || failures == 100)
                wxl::core::log::Warnf("host: WaitAnyRequest unexpected rc=%lu win32=%lu (failure %u)",
                                      rc, GetLastError(), failures);
            if (failures >= 100) return false; // persistent: let the worker retire loudly
            Sleep(10);
        }
        const uint32_t i = rc - WAIT_OBJECT_0;

        const ControlHeader* hdr = ChannelHeader(g_base, i);
        const uint8_t* payload = ChannelPayload(g_base, i);
        channelOut = i;
        seqOut = hdr->reqSeq; // capture the request's sequence so the response can be stamped with it
        uint32_t n = hdr->reqLen;
        if (n > kChannelPayload) n = 0; // malformed: hand the worker an empty request
        reqOut.assign(payload, payload + n);
        return true;
    }

    /**
     * @brief Copies the response payload into channel `i`, marks it complete, and signals the client.
     * @param i     channel index
     * @param seq   request sequence this response belongs to
     * @param resp  response payload bytes
     * @return true if a nonzero-length response was written
     */
    bool PostResponse(uint32_t i, uint32_t seq, std::span<const uint8_t> resp)
    {
        if (i >= g_channelCount) return false;

        ControlHeader* hdr = ChannelHeader(g_base, i);
        uint8_t* payload = ChannelPayload(g_base, i);
        uint32_t n = static_cast<uint32_t>(resp.size());
        if (n > kChannelPayload) n = 0; // never overrun the window; signal a zero-length response
        if (n) memcpy(payload, resp.data(), n);
        hdr->respLen = n;
        // Stamp the response with the sequence of the request it answers -- NOT the current reqSeq, which
        // the client may already have bumped for a newer request after timing this one out. This is what
        // lets the client reject a late response that belongs to an abandoned request.
        hdr->respSeq = seq;
        SetEvent(g_respEv[i]);
        return n != 0;
    }
}
