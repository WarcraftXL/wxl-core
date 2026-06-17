// Storage I/O hook: asset-agnostic forwarder of archive file opens to the host.
// Copyright (C) 2026 WraithEngine
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

#include "StorageHook.hpp"

#include "ShmClient.hpp"
#include "Io.hpp"
#include "Hook.hpp"
#include "Logger.hpp"
#include "WmoMultiPass.hpp"
#include "Adt.hpp"

#include <windows.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace io = wraith::offsets::engine::io;

namespace
{
    // Synthetic file handle, native 0x30-byte file layout. magic at +0x00 (a native handle holds a kind 0..5
    // there) marks ours; +0x14/+0x18/+0x1c match the native size/buffer/position fields the engine may read.
    constexpr uint32_t kHandleMagic = 0x46485257; // 'WRHF'

#pragma pack(push, 1)
    struct WraithFile
    {
        uint32_t magic;        // +0x00  kHandleMagic
        uint32_t hostId;       // +0x04  host file handle (streaming mode; 0 when buffered)
        uint32_t reserved08;   // +0x08
        char*    shortName;    // +0x0c  name (GetFileName)
        char*    fullName;     // +0x10  name (GetFileName)
        uint32_t size;         // +0x14
        uint8_t* buffer;       // +0x18  whole-file bytes when buffered; null in streaming mode; points at the mapping when zero-copy
        uint32_t position;     // +0x1c
        uint32_t reserved20;   // +0x20
        uint32_t reserved24;   // +0x24
        void*    mapView;      // +0x28  blob-section view (zero-copy); null otherwise
        void*    mapHandle;    // +0x2c  blob-section handle (zero-copy); null otherwise
    };
#pragma pack(pop)
    static_assert(sizeof(WraithFile) == 0x30, "WraithFile must match the native 0x30 file layout");

    bool IsOurs(void* h)
    {
        return h && *reinterpret_cast<uint32_t*>(h) == kHandleMagic;
    }

    using OpenFn  = int(__stdcall*)(void* archive, const char* name, uint32_t flags, void** out);
    using SizeFn  = uint32_t(__stdcall*)(void* handle, uint32_t* sizeHigh);
    using ReadFn  = int(__stdcall*)(void* handle, void* dst, uint32_t len, uint32_t* read, void* ovl, uint32_t unk);
    using CloseFn = int(__stdcall*)(void* handle);
    using SeekFn  = uint32_t(__stdcall*)(void* handle, int32_t distLow, uint32_t* distHigh, uint32_t method);

    OpenFn  g_origOpen  = nullptr;
    OpenFn  g_origOpen2 = nullptr;
    SizeFn  g_origSize  = nullptr;
    ReadFn  g_origRead  = nullptr;
    CloseFn g_origClose = nullptr;
    SeekFn  g_origSeek  = nullptr;

    uint32_t g_served = 0; // files served from the host
    uint32_t g_missed = 0; // host connected but file not served (read natively)
    uint32_t g_opens  = 0; // intercept attempts

    bool EndsWithCI(const char* s, const char* suffix)
    {
        size_t ls = strlen(s), lf = strlen(suffix);
        if (lf > ls) return false;
        for (size_t i = 0; i < lf; ++i)
            if (tolower(static_cast<unsigned char>(s[ls - lf + i])) != suffix[i]) return false;
        return true;
    }

    // Names routed to the host. Skips .pub/.url (engine existence probes, never archive content).
    bool ShouldIntercept(const char* name)
    {
        if (!name || name[0] == '\0') return false;
        if (EndsWithCI(name, ".pub") || EndsWithCI(name, ".url")) return false;
        return true;
    }

    char* DupName(const char* s)
    {
        size_t n = strlen(s) + 1;
        char* p = static_cast<char*>(malloc(n));
        if (p) memcpy(p, s, n);
        return p;
    }

    // Serve from the host for both open entry points. Returns true and fills *out with a synthetic handle on a
    // host hit; false lets the native open run.
    bool TryServe(void* archive, const char* name, uint32_t flags, void** out)
    {
        // Specific-archive opens (archive != null) stay native.
        if (archive != nullptr || !ShouldIntercept(name)) return false;

        if ((++g_opens % 2000) == 0)
            WLOG_INFO("Storage stats: opens=%u served=%u missed=%u", g_opens, g_served, g_missed);

        wraith::runtime::ipc::FileOpenResult r = wraith::runtime::ipc::FileOpen(name, flags);
        if (r.ok)
        {
            auto* f = static_cast<WraithFile*>(calloc(1, sizeof(WraithFile)));
            if (f)
            {
                f->magic = kHandleMagic;
                f->size = r.size;
                f->position = 0;
                f->fullName = DupName(name);
                f->shortName = f->fullName;

                bool wholeFile = (flags & io::kOpenWholeFile) != 0;
                const char* mode;
                bool ok = true;
                void* view = nullptr;
                void* mapHandle = nullptr;
                if (r.id == 0)
                {
                    // Inline: bytes came back in the open response.
                    f->buffer = static_cast<uint8_t*>(malloc(r.size ? r.size : 1));
                    if (f->buffer && r.size) memcpy(f->buffer, r.inlineData.data(), r.size);
                    ok = (f->buffer != nullptr);
                    mode = "inline";
                }
                else if (wraith::runtime::ipc::MapBlob(r.id, r.size, view, mapHandle))
                {
                    // Zero-copy: map the host's section read-only and read bytes straight from it.
                    f->buffer = static_cast<uint8_t*>(view);
                    f->mapView = view;
                    f->mapHandle = mapHandle;
                    f->hostId = r.id;
                    mode = "map";
                }
                else if (wholeFile)
                {
                    // Buffered: pull all bytes now, release the host handle.
                    f->buffer = static_cast<uint8_t*>(malloc(r.size ? r.size : 1));
                    uint32_t off = 0;
                    while (f->buffer && off < r.size)
                    {
                        uint32_t n = wraith::runtime::ipc::FileReadChunk(r.id, off, f->buffer + off, r.size - off);
                        if (n == 0) break;
                        off += n;
                    }
                    wraith::runtime::ipc::FileClose(r.id);
                    ok = (f->buffer != nullptr && off == r.size);
                    mode = "whole";
                }
                else
                {
                    // Streaming: keep the host handle, pull chunks on demand.
                    f->buffer = nullptr;
                    f->hostId = r.id;
                    mode = "stream";
                }

                // A served WMO root may carry a trailing WMP1 multi-pass plan, and a merged ADT a trailing ATSC texture-scale table.
                if (ok && f->buffer)
                {
                    f->size = wraith::runtime::wmo::multipass::IngestWmoBytes(name, f->buffer, f->size);
                    f->size = wraith::runtime::adt::IngestAdtBytes(name, f->buffer, f->size);
                }

                if (ok)
                {
                    if (out) *out = f;
                    if (g_served < 60)
                        WLOG_INFO("Storage: serve '%s' (%u B, %s) from host", name, r.size, mode);
                    ++g_served;
                    return true;
                }
                free(f->buffer);
                free(f->fullName);
                free(f);
            }
        }
        else if (wraith::runtime::ipc::IsConnected())
        {
            if (g_missed < 200) WLOG_INFO("Storage: MISS '%s' -> native archive", name);
            ++g_missed;
        }
        return false;
    }

    int __stdcall OpenDetour(void* archive, const char* name, uint32_t flags, void** out)
    {
        if (TryServe(archive, name, flags, out)) return 1;
        return g_origOpen(archive, name, flags, out);
    }

    int __stdcall Open2Detour(void* archive, const char* name, uint32_t flags, void** out)
    {
        if (TryServe(archive, name, flags, out)) return 1;
        return g_origOpen2(archive, name, flags, out);
    }

    uint32_t __stdcall SizeDetour(void* handle, uint32_t* sizeHigh)
    {
        if (IsOurs(handle))
        {
            if (sizeHigh) *sizeHigh = 0;
            return reinterpret_cast<WraithFile*>(handle)->size;
        }
        return g_origSize(handle, sizeHigh);
    }

    int __stdcall ReadDetour(void* handle, void* dst, uint32_t len, uint32_t* read, void* ovl, uint32_t unk)
    {
        if (IsOurs(handle))
        {
            auto* f = reinterpret_cast<WraithFile*>(handle);
            uint32_t avail = (f->position < f->size) ? (f->size - f->position) : 0;
            uint32_t want = (len < avail) ? len : avail;
            uint32_t got = 0;

            if (f->buffer)
            {
                if (want) memcpy(dst, f->buffer + f->position, want);
                got = want;
            }
            else
            {
                uint8_t* p = static_cast<uint8_t*>(dst);
                while (got < want)
                {
                    uint32_t n = wraith::runtime::ipc::FileReadChunk(f->hostId, f->position + got, p + got, want - got);
                    if (n == 0) break;
                    got += n;
                }
            }

            f->position += got;
            if (read) *read = got;
            return (got == len) ? 1 : 0; // nonzero only when the full request was satisfied
        }
        return g_origRead(handle, dst, len, read, ovl, unk);
    }

    uint32_t __stdcall SeekDetour(void* handle, int32_t distLow, uint32_t* distHigh, uint32_t method)
    {
        if (IsOurs(handle))
        {
            auto* f = reinterpret_cast<WraithFile*>(handle);
            int64_t base = (method == 1) ? f->position : (method == 2) ? f->size : 0; // 0=BEGIN,1=CURRENT,2=END
            int64_t pos = base + distLow;
            if (pos < 0) pos = 0;
            if (pos > f->size) pos = f->size;
            f->position = static_cast<uint32_t>(pos);
            if (distHigh) *distHigh = 0;
            return f->position;
        }
        return g_origSeek(handle, distLow, distHigh, method);
    }

    int __stdcall CloseDetour(void* handle)
    {
        if (IsOurs(handle))
        {
            auto* f = reinterpret_cast<WraithFile*>(handle);
            if (f->mapView)
            {
                // Zero-copy: buffer points into the mapping, so unmap (do not free) then release the section.
                wraith::runtime::ipc::UnmapBlob(f->mapView, f->mapHandle);
                wraith::runtime::ipc::FileClose(f->hostId);
            }
            else
            {
                if (!f->buffer && f->hostId) wraith::runtime::ipc::FileClose(f->hostId);
                free(f->buffer);
            }
            free(f->fullName);
            free(f);
            return 1;
        }
        return g_origClose(handle);
    }
}

namespace wraith::runtime::storage
{
    void Install()
    {
        hook::Install("Storage_FileOpen",  io::kFileOpen,  &OpenDetour,  reinterpret_cast<void**>(&g_origOpen));
        hook::Install("Storage_FileOpen2", io::kFileOpen2, &Open2Detour, reinterpret_cast<void**>(&g_origOpen2));
        hook::Install("Storage_FileSize",  io::kFileSize,  &SizeDetour,  reinterpret_cast<void**>(&g_origSize));
        hook::Install("Storage_FileRead",  io::kFileRead,  &ReadDetour,  reinterpret_cast<void**>(&g_origRead));
        hook::Install("Storage_FileSeek",  io::kFileSeek,  &SeekDetour,  reinterpret_cast<void**>(&g_origSeek));
        hook::Install("Storage_FileClose", io::kFileClose, &CloseDetour, reinterpret_cast<void**>(&g_origClose));
        WLOG_INFO("Storage: hooks installed");
    }
}
