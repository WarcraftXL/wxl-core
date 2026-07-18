// DDS (block-compressed) to client texture transcode: payload copies verbatim.
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
#include <vector>

// DDS -> client BLP transcode: a texture-format concern, not a model-format one. Any asset
// pipeline that ships DDS sidecar textures (currently the m3 drop-in flow; potentially any
// future format) calls into this directly rather than owning its own copy.
namespace wxl::modern::assets::textures::dds
{
    /**
     * @brief Transcodes a DXT1/3/5 DDS image to the client texture container.
     * @param data            whole .dds file bytes
     * @param size            byte count
     * @param out             receives the texture bytes
     * @param luminanceAlpha  when true and the source is DXT1 (no alpha channel), derives alpha
     *                        from per-texel luminance and re-encodes as DXT5. Additive-blend FX
     *                        sprites are often authored on a non-black background with no alpha
     *                        channel at all; without this the background shows as a tinted quad.
     * @return true when the input is a supported DDS
     */
    bool DdsToBlp(const uint8_t* data, size_t size, std::vector<uint8_t>& out,
                 bool luminanceAlpha = false);
}
