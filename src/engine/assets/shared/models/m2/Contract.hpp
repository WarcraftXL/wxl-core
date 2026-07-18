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
 * @brief Holds what every m2 theme shares: the accepted source inner-version range and
 *        unaligned byte access into the pre-parse model buffer.
 *
 * Teaches the client to read a modern M2 (the additive superset of the native model) by
 * reshaping it in memory at load time, never by pre-porting files. The work is split by M2
 * sub-system (cameras / particles / ribbons / animations / textures / skin / bones), each theme
 * owning its own deltas for every source version it supports.
 */
namespace wxl::modern::assets::m2
{
    // Source inner-version range this reshapes onto the client contract. A model in this
    // range is a modern superset image; the gate is the range (plus per-theme field/flag presence).
    constexpr uint32_t kSourceVersionMin = 272;
    constexpr uint32_t kSourceVersionMax = 274;

    // Staging marker carried in the inner VERSION field, not in globalFlags (which holds live model
    // data). ProcessInPlace compacts the records and sets version = kClientVersion | this bit: the
    // image is on the client contract but not yet finalized. The DLL clears the bit at load to hand
    // the native parser a clean kClientVersion. A staged image (bit set) means "compacted, just
    // finalize the version and register for the live-engine half"; a source-version image (272-274)
    // means "raw, run the full downport". The client accepts only the exact native version, so a
    // still-staged image is rejected (fail-safe) rather than misparsed if the DLL is absent. No
    // native or modern M2 sets this high bit on its version.
    constexpr uint32_t kStagedVersionBit = 0x40000000;

    /**
     * @brief Reports whether an inner version is staged (compacted, not yet finalized).
     * @param v  Inner version field value.
     * @return True if the staging bit is set.
     */
    inline bool IsStagedVersion(uint32_t v) { return (v & kStagedVersionBit) != 0; }

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
