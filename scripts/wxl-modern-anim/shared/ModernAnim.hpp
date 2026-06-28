// modern-anim shared transform: normalize external M2 animation files for the native loader.
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
#include <span>
#include <string_view>
#include <vector>

/**
 * @brief Shared external-animation byte transform.
 *
 * External .anim files are loaded by the client's later sibling-file path, outside the main M2
 * downport hook. Modern files may be wrapped as AFM2/AFSB chunks even though the client expects the
 * raw track payload; rare MD20-like animation headers are normalized too.
 */
namespace wxl::scripts::modernanim
{
    /**
     * @brief Reports whether an archive path names an external animation file.
     * @param name  Archive-relative asset path.
     * @return True when the suffix is ".anim", case-insensitively.
     */
    bool IsAnimName(std::string_view name);

    /**
     * @brief Normalizes a loaded external-animation buffer in place.
     * @param bytes  Mutable file bytes.
     * @param size   Byte length.
     * @return True if the buffer was recognized and rewritten.
     */
    bool ProcessInPlaceAndResize(void* bytes, uint32_t& size);

    /**
     * @brief Normalizes a loaded external-animation buffer in place, preserving the caller's size value.
     * @param bytes  Mutable file bytes.
     * @param size   Byte length.
     * @return True if the buffer was recognized and rewritten.
     */
    bool ProcessInPlace(void* bytes, uint32_t size);

    /**
     * @brief Copies and normalizes an external-animation buffer.
     * @param raw  Source file bytes.
     * @param out  Receives the normalized copy on success.
     * @return True if the buffer was recognized and copied to out.
     */
    bool ProcessCopy(std::span<const uint8_t> raw, std::vector<uint8_t>& out);
}
