// IPC protocol: the single versioned source of truth for the host <-> DLL contract.
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
#include <cstdio>

// Compiled into both processes (32-bit DLL and 64-bit host). The shared-memory window is split into
// independent channels, each with its own control header and payload region. Payloads are FlexBuffers,
// endian and arch neutral so the 32-bit DLL and 64-bit host agree on bytes. Any wire change bumps
// kVersion; the DLL rejects a host whose version differs.
//
// Named OS objects are scoped per client process id so concurrent Wow.exe instances each get an
// isolated host session (mailbox, events, blob sections, singleton mutex).
namespace wxl::ipc
{
    constexpr uint32_t kMinChannels = 2;
    constexpr uint32_t kMaxChannels = 16;

    // Format strings: first %u is always the session (client) PID.
    constexpr const char* kShmNameFmt        = "Local\\WarcraftXLShm_%u";
    constexpr const char* kReqEventFmt       = "Local\\WarcraftXLReq_%u_%u";
    constexpr const char* kRespEventFmt      = "Local\\WarcraftXLResp_%u_%u";
    constexpr const char* kBlobNameFmt       = "Local\\WarcraftXLBlob_%u_%u";
    constexpr const char* kHostSingletonFmt  = "Local\\WarcraftXLHostSingleton_%u";

    constexpr uint32_t kMagic   = 0x4C585857; // 'WXXL'
    constexpr uint32_t kVersion = 1;

    constexpr uint32_t kHeaderSize     = 64;
    constexpr uint32_t kFileChunkMax   = 512u * 1024u;
    // Files at or below this size come back inline in the open response (one small copy). Larger files
    // are served zero-copy via a shared blob section the client maps directly. Inline saves the section
    // create/map plus the close round trip, so mid-size files (most textures) stay on the one-trip path;
    // the response must still fit kChannelPayload with headroom for the FlexBuffers envelope.
    constexpr uint32_t kInlineMax      = 256u * 1024u;
    constexpr uint32_t kChannelPayload = 768u * 1024u;
    constexpr uint32_t kChannelStride  = kHeaderSize + kChannelPayload;

    /** @brief Returns the shared window size for a channel count chosen at runtime. */
    inline uint32_t ShmSize(uint32_t channelCount) { return channelCount * kChannelStride; }

    /** @brief Request kind, carried inside the FlexBuffers payload. */
    enum Op : uint32_t
    {
        OpFileOpen   = 4, // name, flags     -> inline blob (small) OR blob id + size (zero-copy)
        OpFileRead   = 5, // blobId, off, len -> status, blob (fallback; sectioned files map direct)
        OpFileClose  = 6, // blobId          -> status (releases the blob section)
        OpFileExists = 7, // name            -> status (StOk = present)
    };

    /** @brief Response status carried inside the FlexBuffers payload. */
    enum Status : uint32_t { StOk = 0, StNotFound = 1, StBadRequest = 2 };

#pragma pack(push, 4)
    /** @brief Per-channel control header: fixed-width, no pointers, identical across 32 and 64 bit. */
    struct ControlHeader
    {
        uint32_t magic;        // kMagic
        uint32_t version;      // kVersion
        uint32_t reqSeq;       // client bumps after writing a request
        uint32_t respSeq;      // host sets == reqSeq after writing the response
        uint32_t reqLen;       // request payload length
        uint32_t respLen;      // response payload length
        uint32_t channelCount; // total channel count the host created (only meaningful on channel 0)
        uint32_t reserved[9];  // pad to kHeaderSize
    };
#pragma pack(pop)

    static_assert(sizeof(ControlHeader) == kHeaderSize, "ControlHeader must be 64 bytes");

    // Channel layout helpers: window is [header|payload] * channelCount, contiguous.

    /** @brief Returns the byte offset of channel `i`'s control header within the window. */
    inline uint32_t ChannelHeaderOffset(uint32_t i)  { return i * kChannelStride; }
    /** @brief Returns the byte offset of channel `i`'s payload within the window. */
    inline uint32_t ChannelPayloadOffset(uint32_t i) { return i * kChannelStride + kHeaderSize; }

    /**
     * @brief Returns a pointer to channel `i`'s control header within the mapped window.
     * @param base  base address of the mapped window
     * @param i     channel index
     * @return pointer to the channel's control header
     */
    inline ControlHeader* ChannelHeader(uint8_t* base, uint32_t i)
    {
        return reinterpret_cast<ControlHeader*>(base + ChannelHeaderOffset(i));
    }
    /**
     * @brief Returns a pointer to channel `i`'s payload within the mapped window.
     * @param base  base address of the mapped window
     * @param i     channel index
     * @return pointer to the channel's payload region
     */
    inline uint8_t* ChannelPayload(uint8_t* base, uint32_t i)
    {
        return base + ChannelPayloadOffset(i);
    }

    // Format the per-session / per-channel request/response/blob names into the caller's buffer
    // (>= 64 bytes). sessionPid is the Wow.exe process id that owns this host session.

    /**
     * @brief Formats the shared mailbox name for `sessionPid` into the caller's buffer.
     * @param out         destination buffer (>= 64 bytes)
     * @param cap         buffer capacity
     * @param sessionPid  client process id that owns the session
     */
    inline void ShmName(char* out, size_t cap, uint32_t sessionPid)
    {
        _snprintf_s(out, cap, _TRUNCATE, kShmNameFmt, sessionPid);
    }
    /**
     * @brief Formats the host singleton mutex name for `sessionPid` into the caller's buffer.
     * @param out         destination buffer (>= 64 bytes)
     * @param cap         buffer capacity
     * @param sessionPid  client process id that owns the session
     */
    inline void HostSingletonName(char* out, size_t cap, uint32_t sessionPid)
    {
        _snprintf_s(out, cap, _TRUNCATE, kHostSingletonFmt, sessionPid);
    }
    /**
     * @brief Formats channel `i`'s request event name into the caller's buffer.
     * @param out         destination buffer (>= 64 bytes)
     * @param cap         buffer capacity
     * @param sessionPid  client process id that owns the session
     * @param i           channel index
     */
    inline void ReqEventName(char* out, size_t cap, uint32_t sessionPid, uint32_t i)
    {
        _snprintf_s(out, cap, _TRUNCATE, kReqEventFmt, sessionPid, i);
    }
    /**
     * @brief Formats channel `i`'s response event name into the caller's buffer.
     * @param out         destination buffer (>= 64 bytes)
     * @param cap         buffer capacity
     * @param sessionPid  client process id that owns the session
     * @param i           channel index
     */
    inline void RespEventName(char* out, size_t cap, uint32_t sessionPid, uint32_t i)
    {
        _snprintf_s(out, cap, _TRUNCATE, kRespEventFmt, sessionPid, i);
    }
    /**
     * @brief Formats the blob section name for `id` into the caller's buffer.
     * @param out         destination buffer (>= 64 bytes)
     * @param cap         buffer capacity
     * @param sessionPid  client process id that owns the session
     * @param id          blob id
     */
    inline void BlobName(char* out, size_t cap, uint32_t sessionPid, uint32_t id)
    {
        _snprintf_s(out, cap, _TRUNCATE, kBlobNameFmt, sessionPid, id);
    }
}
