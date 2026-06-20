// IO game bindings: typed inline wrappers over the client's archive file-I/O primitives.
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

#include "game/Binding.hpp"
#include "offsets/engine/Io.hpp"

/**
 * @brief Typed inline wrappers over the client archive file-I/O primitives, exposed as the IO binding catalog.
 */
namespace wxl::game::io
{
    namespace off = wxl::offsets::engine::io;

    /**
     * @brief Opens a file by name.
     * @param name       File name to open.
     * @param flags      Open flags.
     * @param outHandle  Receives the open file handle.
     * @return Nonzero on success.
     */
    inline int FileOpen(const char* name, uint32_t flags, void** outHandle)
    {
        return Native<off::Storage_FileOpenFn>(off::kFileOpen)(nullptr, name, flags, outHandle);
    }

    /**
     * @brief Reads the size of an open file.
     * @param handle    Open file handle.
     * @param sizeHigh  Receives the high dword of the size.
     * @return The low dword of the size.
     */
    inline uint32_t FileSize(void* handle, uint32_t* sizeHigh)
    {
        return Native<off::Storage_FileSizeFn>(off::kFileSize)(handle, sizeHigh);
    }

    /**
     * @brief Reads len bytes from an open file into dst.
     * @param handle  Open file handle.
     * @param dst     Destination buffer.
     * @param len     Byte count to read.
     * @param read    Receives the number of bytes read.
     * @return Nonzero on success.
     */
    inline int FileRead(void* handle, void* dst, uint32_t len, uint32_t* read)
    {
        return Native<off::Storage_FileReadFn>(off::kFileRead)(handle, dst, len, read, nullptr, 0);
    }

    /**
     * @brief Seeks within an open file.
     * @param handle    Open file handle.
     * @param distLow   Low dword of the signed seek distance.
     * @param distHigh  High dword of the seek distance.
     * @param method    Seek origin (0=begin, 1=current, 2=end).
     * @return The low dword of the new position.
     */
    inline uint32_t FileSeek(void* handle, int32_t distLow, uint32_t* distHigh, uint32_t method)
    {
        return Native<off::Storage_FileSeekFn>(off::kFileSeek)(handle, distLow, distHigh, method);
    }

    /**
     * @brief Closes an open file handle.
     * @param handle  Open file handle.
     * @return Nonzero on success.
     */
    inline int FileClose(void* handle)
    {
        return Native<off::Storage_FileCloseFn>(off::kFileClose)(handle);
    }

    /** @brief Adds the IO bindings to the enumerable catalog. */
    inline void RegisterCatalog()
    {
        Register({ "IO::FileOpen",  off::kFileOpen,  "int(name, flags, &handle)" });
        Register({ "IO::FileRead",  off::kFileRead,  "int(handle, dst, len, &read)" });
        Register({ "IO::FileSize",  off::kFileSize,  "uint32(handle, &sizeHigh)" });
        Register({ "IO::FileSeek",  off::kFileSeek,  "uint32(handle, distLow, &distHigh, method)" });
        Register({ "IO::FileClose", off::kFileClose, "int(handle)" });
    }
}
