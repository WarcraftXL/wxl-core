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

#pragma once

#include <cstddef>
#include <cstdint>

/// Live byte patching of the client image.
namespace wxl::core::mem
{
    /**
     * @brief Copies len bytes from src into dst, toggling page protection around the write.
     * @param dst  destination address in the client image.
     * @param src  source bytes to copy.
     * @param len  number of bytes to write.
     * @return true if the write succeeded.
     */
    bool Patch(void* dst, const void* src, size_t len);

    /**
     * @brief Writes len copies of value at dst.
     * @param dst    destination address in the client image.
     * @param value  byte to repeat (0x90 fills with NOP).
     * @param len    number of bytes to write.
     * @return true if the write succeeded.
     */
    bool Fill(void* dst, uint8_t value, size_t len);
}
