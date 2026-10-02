// Archive file-I/O primitive addresses, their signatures, and the whole-file open flag.
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

// INTERNAL to the core. Archive file-I/O primitives, all callee-cleaned. Hooked to serve reads from
// an external source. Modules never include this; they use wxl::game / wxl::events.
namespace wxl::offsets::engine::io
{
    // Open (archiveOrNull, name, flags, &handle) -> nonzero, fills handle.
    constexpr uintptr_t kFileOpen  = 0x00424B50;
    constexpr uintptr_t kFileOpen2 = 0x00422040;
    // Size (handle, &sizeHigh) -> file size low dword.
    constexpr uintptr_t kFileSize  = 0x004218C0;
    // Read (handle, dst, len, &read|0, 0, 0) -> nonzero.
    constexpr uintptr_t kFileRead  = 0x00422530;
    // Seek (handle, distLow, &distHigh, method) -> new position low; method 0=begin,1=current,2=end.
    constexpr uintptr_t kFileSeek  = 0x00421BB0;
    // Close (handle).
    constexpr uintptr_t kFileClose = 0x00422910;

    // Mount-into-search-chain primitive (boot's own bookkeeping wrapper). Superseded as a hook point by
    // kMopaqOpenArchive below, which every mount -- including this one -- ultimately calls into; kept as
    // a landmark, not actively hooked.
    constexpr uintptr_t kArchiveMount = 0x00421950;
    // The true archive/directory mount primitive: every native mount call in the client (boot's base+
    // patch table, the per-patch nested "alternate.MPQ" probe, the Survey/patch-download runtime mounts)
    // funnels through here. (name, priority, flags, &out) -> nonzero on success; returning 0 reads as an
    // absent optional archive, which every caller already tolerates. NOT __stdcall like kArchiveMount --
    // this one is __cdecl (caller-cleaned stack).
    constexpr uintptr_t kMopaqOpenArchive = 0x0045C480;
    using MopaqOpenArchiveFn = char(__cdecl*)(const char* name, int priority, uint32_t flags, void** out);

    // Second required-archive gate inside kInitializeWowConfig (`jne 0x4061d4`). A failed mount for one of
    // a small set of rows that are required for this install (common.MPQ, locale-<locale>.MPQ,
    // speech-<locale>.MPQ) falls through into a hard "Failed to open archive %s." dialog + exit() --
    // every other row already takes the tolerant "decrement slot, mark handled, continue" path on a
    // failed mount. Patched to jump unconditionally so a required row gets the same tolerance. Kept
    // applied as a harmless safety net (a genuinely missing/corrupted required archive is now a logged
    // skip instead of a hard dialog+exit) even with native mounting otherwise unchanged.
    constexpr uintptr_t kRequiredArchiveGateJnz = 0x004060BD;

    // Boot's archive-mount + signature-file-check + expansion-tier-flag driver (void, no args). Hooked
    // "call original, then extend": forces the expansion-content-present flag below to its full value
    // right after the original runs, as a harmless idempotent safety net -- the native derivation
    // already produces the same value on its own when the expansion archives mount normally.
    constexpr uintptr_t kInitializeWowConfig = 0x00405DD0;
    using InitializeWowConfigFn = void(__cdecl*)();

    // Expansion-content-present tri-state (0=classic, 1=TBC, 2=current). The login/realm-select gate and
    // the Lua client-expansion-level API both read this.
    constexpr uintptr_t kWotlkContentFlag = 0x00B2F9E1;

    // Open flag: load the whole file into the handle buffer.
    constexpr uint32_t  kOpenWholeFile = 0x20000;

    using Storage_FileOpenFn  = int(__stdcall*)(void* archive, const char* name, uint32_t flags, void** out);
    using Storage_FileSizeFn  = uint32_t(__stdcall*)(void* handle, uint32_t* sizeHigh);
    using Storage_FileReadFn  = int(__stdcall*)(void* handle, void* dst, uint32_t len, uint32_t* read, void* ovl, uint32_t unk);
    using Storage_FileSeekFn  = uint32_t(__stdcall*)(void* handle, int32_t distLow, uint32_t* distHigh, uint32_t method);
    using Storage_FileCloseFn = int(__stdcall*)(void* handle);
    using Storage_ArchiveMountFn = int(__stdcall*)(const char* name, int priority, uint32_t flags, void** out);
}
