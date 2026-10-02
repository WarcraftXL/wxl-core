// The Client's statically linked zlib 1.2.2: the three entry points an inflate of our own needs.
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

// zlib 1.2.2, linked into the Client (its banner string sits at 0x00A4BFE8). Reached directly rather
// than through one of the Client's three wrappers (zlib_decompress, ZLibUnPack, WardenZlibDecompress):
// those own their output buffer policy, and a caller that already knows its exact output size wants the
// stream API. Standard zlib ABI, __cdecl, so the declarations below are the published signatures.
namespace wxl::offsets::engine::zlib
{
    /// int inflateInit_(z_streamp, const char* version, int streamSize). The Client passes "1.2.2" and
    /// 0x38; the size argument is a version guard inside zlib and MUST match sizeof(ZStream) below.
    constexpr uintptr_t kInflateInit = 0x00865150;
    /// int inflate(z_streamp, int flush). Named FUN_00865270 in the export; identified as inflate by
    /// its position between inflateInit_ and inflateEnd and by all three Client wrappers calling it
    /// with flush = 4 (Z_FINISH) between an inflateInit_ and an inflateEnd.
    constexpr uintptr_t kInflate = 0x00865270;
    /// int inflateEnd(z_streamp).
    constexpr uintptr_t kInflateEnd = 0x00866660;

    constexpr int kZOk         = 0;
    constexpr int kZStreamEnd  = 1;
    constexpr int kZFinish     = 4;
    constexpr const char* kVersion = "1.2.2";

    /**
     * @brief z_stream as zlib 1.2.2 lays it out for a 32-bit build.
     *
     * Its size is a hard contract: inflateInit_ compares the caller's `streamSize` against its own
     * sizeof and refuses with Z_VERSION_ERROR on a mismatch, which is why the Client passes 0x38.
     */
    struct ZStream
    {
        const uint8_t* nextIn;
        uint32_t       availIn;
        uint32_t       totalIn;
        uint8_t*       nextOut;
        uint32_t       availOut;
        uint32_t       totalOut;
        const char*    msg;
        void*          state;
        void*          zalloc;    ///< null lets inflateInit_ install zlib's own allocator
        void*          zfree;
        void*          opaque;
        int32_t        dataType;
        uint32_t       adler;
        uint32_t       reserved;
    };
    static_assert(sizeof(ZStream) == 0x38, "z_stream size is the version guard inflateInit_ checks");

    using InflateInitFn = int(__cdecl*)(ZStream*, const char* version, int streamSize);
    using InflateFn     = int(__cdecl*)(ZStream*, int flush);
    using InflateEndFn  = int(__cdecl*)(ZStream*);
}
