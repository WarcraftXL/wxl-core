// Shared contract for the m2 themes: source version range and unaligned byte access.
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
#include <cstring>

/**
 * @brief What the m2 themes share: the modern inner-version range the native reader accepts, and
 *        unaligned byte access into the pre-parse model buffer.
 */
namespace wxl::modern::assets::m2
{
    // Modern MD21 inner-version window the native reader (features/m2native) reads directly.
    constexpr uint32_t kSourceVersionMin = 272;
    constexpr uint32_t kSourceVersionMax = 274;

    // Unaligned little-endian access. Pre-parse, the model buffer's array offsets are model-relative,
    // so the themes read/write through these into md->base() + offset.

    /** @brief Reads an unaligned little-endian u32 at p. @param p Byte pointer. @return The u32 value. */
    inline uint32_t Rd32(const uint8_t* p)       { uint32_t v; std::memcpy(&v, p, 4); return v; }
    /** @brief Writes an unaligned little-endian u32 at p. @param p Byte pointer. @param v Value to write. */
    inline void     Wr32(uint8_t* p, uint32_t v) { std::memcpy(p, &v, 4); }
    /** @brief Reads an unaligned little-endian u16 at p. @param p Byte pointer. @return The u16 value. */
    inline uint16_t Rd16(const uint8_t* p)       { uint16_t v; std::memcpy(&v, p, 2); return v; }
    /** @brief Writes an unaligned little-endian u16 at p. @param p Byte pointer. @param v Value to write. */
    inline void     Wr16(uint8_t* p, uint16_t v) { std::memcpy(p, &v, 2); }
}
