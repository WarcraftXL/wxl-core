// Compact DXT5 (BC3) block compressor: range-fit endpoints, enough for smooth gradient textures.
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
#include <cstdlib>

// One 4x4 RGBA block -> 16-byte DXT5 (BC3): an 8-byte alpha block (2 endpoints + 16 3-bit indices) then an
// 8-byte DXT1 color block (2 RGB565 endpoints + 16 2-bit indices). Range-fit (min/max endpoints): not the
// optimal compressor, but correct and decodable, and good enough for the smooth sky gradients that need it.
namespace wxl::modern::assets::textures::blp::dxt
{
    inline uint16_t Pack565(int r, int g, int b)
    {
        return static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    }
    inline void Unpack565(uint16_t c, int& r, int& g, int& b)
    {
        r = ((c >> 11) & 0x1F) << 3; g = ((c >> 5) & 0x3F) << 2; b = (c & 0x1F) << 3;
    }

    // px = 16 RGBA texels (row-major 4x4). out = 16 bytes.
    inline void CompressBlock(const uint8_t px[64], uint8_t out[16])
    {
        // --- alpha (BC3 alpha) ---
        uint8_t amin = 255, amax = 0;
        for (int i = 0; i < 16; ++i) { uint8_t a = px[i * 4 + 3]; if (a < amin) amin = a; if (a > amax) amax = a; }
        out[0] = amax; out[1] = amin; // a0 >= a1 -> 8-alpha interpolation mode
        uint8_t al[8];
        al[0] = amax; al[1] = amin;
        for (int i = 1; i < 7; ++i) al[i + 1] = static_cast<uint8_t>(((7 - i) * amax + i * amin) / 7);
        uint64_t aidx = 0;
        for (int i = 0; i < 16; ++i)
        {
            int a = px[i * 4 + 3], best = 0, bd = 1 << 30;
            for (int j = 0; j < 8; ++j) { int d = std::abs(a - al[j]); if (d < bd) { bd = d; best = j; } }
            aidx |= static_cast<uint64_t>(best) << (3 * i);
        }
        for (int i = 0; i < 6; ++i) out[2 + i] = static_cast<uint8_t>(aidx >> (8 * i));

        // --- color (DXT1) ---
        int rmn = 255, gmn = 255, bmn = 255, rmx = 0, gmx = 0, bmx = 0;
        for (int i = 0; i < 16; ++i)
        {
            int r = px[i * 4], g = px[i * 4 + 1], b = px[i * 4 + 2];
            if (r < rmn) rmn = r; if (g < gmn) gmn = g; if (b < bmn) bmn = b;
            if (r > rmx) rmx = r; if (g > gmx) gmx = g; if (b > bmx) bmx = b;
        }
        uint16_t c0 = Pack565(rmx, gmx, bmx), c1 = Pack565(rmn, gmn, bmn);
        if (c0 < c1) { uint16_t t = c0; c0 = c1; c1 = t; } // c0 > c1 -> 4-colour mode
        out[8] = uint8_t(c0); out[9] = uint8_t(c0 >> 8); out[10] = uint8_t(c1); out[11] = uint8_t(c1 >> 8);
        if (c0 == c1) { out[12] = out[13] = out[14] = out[15] = 0; return; } // flat block, all index 0

        int cr[4], cg[4], cb[4];
        Unpack565(c0, cr[0], cg[0], cb[0]);
        Unpack565(c1, cr[1], cg[1], cb[1]);
        for (int k = 0; k < 3; ++k)
        {
            int* e = (k == 0) ? cr : (k == 1) ? cg : cb;
            e[2] = (2 * e[0] + e[1]) / 3; e[3] = (e[0] + 2 * e[1]) / 3;
        }
        uint32_t cidx = 0;
        for (int i = 0; i < 16; ++i)
        {
            int r = px[i * 4], g = px[i * 4 + 1], b = px[i * 4 + 2], best = 0, bd = 1 << 30;
            for (int j = 0; j < 4; ++j)
            {
                int dr = r - cr[j], dg = g - cg[j], db = b - cb[j], d = dr * dr + dg * dg + db * db;
                if (d < bd) { bd = d; best = j; }
            }
            cidx |= static_cast<uint32_t>(best) << (2 * i);
        }
        for (int i = 0; i < 4; ++i) out[12 + i] = static_cast<uint8_t>(cidx >> (8 * i));
    }
}
