// In-process live byte patching of the client image.
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

#include "core/Mem.hpp"

#include <windows.h>
#include <cstring>

namespace wxl::core::mem
{
    /**
     * @brief Copies len bytes from src into dst, toggling page protection around the write.
     * @param dst  destination address in the client image.
     * @param src  source bytes to copy.
     * @param len  number of bytes to write.
     * @return true if the write succeeded.
     */
    bool Patch(void* dst, const void* src, size_t len)
    {
        DWORD old = 0;
        if (!VirtualProtect(dst, len, PAGE_EXECUTE_READWRITE, &old)) return false;
        std::memcpy(dst, src, len);
        VirtualProtect(dst, len, old, &old);
        FlushInstructionCache(GetCurrentProcess(), dst, len);
        return true;
    }

    /**
     * @brief Writes len copies of value at dst.
     * @param dst    destination address in the client image.
     * @param value  byte to repeat.
     * @param len    number of bytes to write.
     * @return true if the write succeeded.
     */
    bool Fill(void* dst, uint8_t value, size_t len)
    {
        DWORD old = 0;
        if (!VirtualProtect(dst, len, PAGE_EXECUTE_READWRITE, &old)) return false;
        std::memset(dst, value, len);
        VirtualProtect(dst, len, old, &old);
        FlushInstructionCache(GetCurrentProcess(), dst, len);
        return true;
    }
}
