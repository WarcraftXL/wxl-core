// BLP2 decoder: mip-0 decode to 32-bit BGRA, ported minimally into core (no external module dependency).
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
#include <vector>

/// A small, self-contained BLP2 reader for the UI texture loader. It decodes only mip 0 to 32-bit BGRA
/// (the byte order of D3DFMT_A8R8G8B8: bytes [B, G, R, A] per pixel), which is exactly what CreateTexture +
/// LockRect expects. Logic is ported from the sibling wxl-modern-assets BLP module so core carries no build
/// dependency on it. Covers the encodings 3.3.5 assets actually use; anything else returns false.
namespace wxl::lua::ui
{
    /**
     * @brief Decodes BLP2 mip 0 into a tightly packed 32-bit BGRA image.
     *
     * Supported: encoding 1 (palettized, alphaDepth 0/1/4/8), encoding 2 (DXT: DXT1 alphaType 0, DXT3
     * alphaType 1, DXT5 alphaType 7) and encoding 3 (uncompressed BGRA). BLP1 and JPEG-BLP are rejected
     * (they are not used by 3.3.5 icons/UI art). All reads are bounds-checked against size.
     *
     * @param data     the raw .blp bytes.
     * @param size     the byte count of data.
     * @param width    receives the mip-0 width on success.
     * @param height   receives the mip-0 height on success.
     * @param outBgra  receives width*height*4 bytes of BGRA (cleared/failed to empty on error).
     * @return true on a successful decode, false for a non-BLP2, an unsupported encoding, or a truncation.
     */
    bool DecodeBlpToBgra(const uint8_t* data, size_t size, int& width, int& height,
                         std::vector<uint8_t>& outBgra);
}
