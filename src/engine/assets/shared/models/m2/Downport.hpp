// Reshapes a source MD20 image onto the client M2 contract before the native parser reads it.
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
#include <string_view>

/**
 * @brief Pre-parse orchestrator that reshapes a source MD20 image onto the client M2 contract.
 *
 * The header and body share the client's field layout, but a few records are wider in the source.
 * Each M2 theme's compaction (cameras / particles / ribbons / animations) slides them onto the
 * client strides and normalizes the source-only encodings, then the inner version is rewritten to
 * the client's. Most work is in place; only the synthesized texture-coordinate-combos array
 * (textures theme) appends two bytes, which grows the buffer.
 *
 * Allocation-agnostic: it never allocates. The caller owns the buffer (the model allocator in the
 * DLL, a std::vector in the host), so the same byte-transform compiles into both. The caller sizes
 * with WorkSize, copies the source bytes into its own buffer, then calls ProcessInPlace.
 */
namespace wxl::modern::assets::m2::downport
{
    /**
     * @brief Reports whether a buffer is an MD20 image in the source version range this module reshapes.
     * @param buffer  Model bytes.
     * @param size    Buffer length.
     * @return True if the image is in the source range.
     */
    bool IsConvertible(const void* buffer, uint32_t size);

    /**
     * @brief Logs a model's header (version, flags, counts).
     * @param buffer  Model bytes.
     * @param size    Buffer length.
     */
    void Inspect(const void* buffer, uint32_t size);

    /**
     * @brief Computes the byte count the reshaped image needs.
     *
     * Equals size, plus two bytes for the synthesized texture-coordinate-combos tail when the source
     * omits textureUnitLookup. Equals size for a non-source image.
     * @param buffer  Source model bytes.
     * @param size    Source length.
     * @return Required work-buffer size in bytes.
     */
    uint32_t WorkSize(const void* buffer, uint32_t size, std::string_view name = {});

    /**
     * @brief Reshapes an already-copied image in place onto the client contract.
     *
     * work holds workSize bytes, the first origSize copied verbatim from the source. On success the
     * inner version is rewritten to the client value.
     * @param work      Work buffer holding the copied source bytes.
     * @param origSize  Length of the copied source.
     * @param workSize  Total work-buffer length.
     * @return True on success; false when work is not a source image or workSize is too small for the grow.
     */
    bool ProcessInPlace(void* work, uint32_t origSize, uint32_t workSize, std::string_view name = {});
}
