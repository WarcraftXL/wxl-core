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
// kChannels independent channels, each with its own control header and payload region. Payloads are
// FlexBuffers, endian and arch neutral so the 32-bit DLL and 64-bit host agree on bytes. Any wire change
// bumps kVersion; the DLL rejects a host whose version differs.
namespace wxl::ipc
{
    // The client serializes its opens onto one request slot, so a single channel is enough; the host
    // serves it on its main thread.
    constexpr uint32_t kChannels = 1;

    constexpr const char* kShmName      = "Local\\WarcraftXLShm";
    constexpr const char* kReqEventFmt  = "Local\\WarcraftXLReq_%u";
    constexpr const char* kRespEventFmt = "Local\\WarcraftXLResp_%u";
    // Per-file zero-copy blob: the host puts a served file's whole bytes in a shared section named with
    // its blob id; the client maps that section directly (no chunking, no copy through a channel).
    constexpr const char* kBlobNameFmt  = "Local\\WarcraftXLBlob_%u";

    constexpr uint32_t kMagic   = 0x4C585857; // 'WXXL'
    constexpr uint32_t kVersion = 1;

    constexpr uint32_t kHeaderSize     = 64;
    constexpr uint32_t kFileChunkMax   = 512u * 1024u;
    // Files at or below this size come back inline in the open response (one small copy). Larger files
    // are served zero-copy via a shared blob section the client maps directly.
    constexpr uint32_t kInlineMax      = 64u * 1024u;
    constexpr uint32_t kChannelPayload = 768u * 1024u;
    constexpr uint32_t kChannelStride  = kHeaderSize + kChannelPayload;
    constexpr uint32_t kShmSize        = kChannels * kChannelStride;

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
        uint32_t reserved[10]; // pad to kHeaderSize
    };
#pragma pack(pop)

    static_assert(sizeof(ControlHeader) == kHeaderSize, "ControlHeader must be 64 bytes");

    // Channel layout helpers: window is [header|payload] * kChannels, contiguous.

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

    // Format the per-channel request/response/blob names into the caller's buffer (>= 32 bytes).

    /**
     * @brief Formats channel `i`'s request event name into the caller's buffer.
     * @param out  destination buffer (>= 32 bytes)
     * @param cap  buffer capacity
     * @param i    channel index
     */
    inline void ReqEventName(char* out, size_t cap, uint32_t i)  { _snprintf_s(out, cap, _TRUNCATE, kReqEventFmt, i); }
    /**
     * @brief Formats channel `i`'s response event name into the caller's buffer.
     * @param out  destination buffer (>= 32 bytes)
     * @param cap  buffer capacity
     * @param i    channel index
     */
    inline void RespEventName(char* out, size_t cap, uint32_t i) { _snprintf_s(out, cap, _TRUNCATE, kRespEventFmt, i); }
    /**
     * @brief Formats the blob section name for `id` into the caller's buffer.
     * @param out  destination buffer (>= 32 bytes)
     * @param cap  buffer capacity
     * @param id   blob id
     */
    inline void BlobName(char* out, size_t cap, uint32_t id)     { _snprintf_s(out, cap, _TRUNCATE, kBlobNameFmt, id); }
}
