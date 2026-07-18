// DDS to client texture transcode implementation.
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

#include "Dds.hpp"

#include <algorithm>
#include <cstring>

namespace wxl::modern::assets::textures::dds
{
    namespace
    {
        inline uint32_t Rd32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }

        void Unpack565(uint16_t c, int& r, int& g, int& b)
        {
            r = ((c >> 11) & 0x1F) * 255 / 31;
            g = ((c >> 5) & 0x3F) * 255 / 63;
            b = (c & 0x1F) * 255 / 31;
        }

        /** @brief Decodes one 8-byte DXT1 color block (opaque mode) to 16 RGB texels. */
        void DecodeDxt1Block(const uint8_t* block, uint8_t rgb[48])
        {
            uint16_t c0, c1;
            std::memcpy(&c0, block, 2);
            std::memcpy(&c1, block + 2, 2);
            int r[4], g[4], b[4];
            Unpack565(c0, r[0], g[0], b[0]);
            Unpack565(c1, r[1], g[1], b[1]);
            r[2] = (2 * r[0] + r[1]) / 3; g[2] = (2 * g[0] + g[1]) / 3; b[2] = (2 * b[0] + b[1]) / 3;
            r[3] = (r[0] + 2 * r[1]) / 3; g[3] = (g[0] + 2 * g[1]) / 3; b[3] = (b[0] + 2 * b[1]) / 3;
            uint32_t idx;
            std::memcpy(&idx, block + 4, 4);
            for (int i = 0; i < 16; ++i)
            {
                const int k = (idx >> (2 * i)) & 3;
                rgb[i * 3 + 0] = uint8_t(r[k]); rgb[i * 3 + 1] = uint8_t(g[k]); rgb[i * 3 + 2] = uint8_t(b[k]);
            }
        }

        /** @brief Builds an 8-byte BC3 alpha block (8-level interpolation, full 0..255 range). */
        void EncodeAlphaBlock(const uint8_t alpha[16], uint8_t out[8])
        {
            out[0] = 255; out[1] = 0; // a0 > a1 -> 8-alpha interpolation mode
            static const int levels[8] = { 255, 0, 219, 182, 146, 109, 73, 36 };
            uint64_t idx = 0;
            for (int i = 0; i < 16; ++i)
            {
                int best = 0, bd = 1 << 30;
                for (int j = 0; j < 8; ++j)
                {
                    const int d = std::abs(int(alpha[i]) - levels[j]);
                    if (d < bd) { bd = d; best = j; }
                }
                idx |= uint64_t(best) << (3 * i);
            }
            for (int i = 0; i < 6; ++i) out[2 + i] = uint8_t(idx >> (8 * i));
        }

        /** @brief Range-fit DXT1 color block encoder (min/max endpoints, nearest-index match). */
        void EncodeColorBlock(const uint8_t rgb[48], uint8_t out[8])
        {
            int rmn = 255, gmn = 255, bmn = 255, rmx = 0, gmx = 0, bmx = 0;
            for (int i = 0; i < 16; ++i)
            {
                const int r = rgb[i * 3], g = rgb[i * 3 + 1], b = rgb[i * 3 + 2];
                rmn = std::min(rmn, r); gmn = std::min(gmn, g); bmn = std::min(bmn, b);
                rmx = std::max(rmx, r); gmx = std::max(gmx, g); bmx = std::max(bmx, b);
            }
            auto pack565 = [](int r, int g, int b) {
                return uint16_t(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            };
            uint16_t c0 = pack565(rmx, gmx, bmx), c1 = pack565(rmn, gmn, bmn);
            if (c0 < c1) std::swap(c0, c1); // c0 > c1 selects the 4-colour (no punch-through) mode
            std::memcpy(out, &c0, 2);
            std::memcpy(out + 2, &c1, 2);
            if (c0 == c1) { out[4] = out[5] = out[6] = out[7] = 0; return; }

            int cr[4], cg[4], cb[4];
            Unpack565(c0, cr[0], cg[0], cb[0]);
            Unpack565(c1, cr[1], cg[1], cb[1]);
            cr[2] = (2 * cr[0] + cr[1]) / 3; cg[2] = (2 * cg[0] + cg[1]) / 3; cb[2] = (2 * cb[0] + cb[1]) / 3;
            cr[3] = (cr[0] + 2 * cr[1]) / 3; cg[3] = (cg[0] + 2 * cg[1]) / 3; cb[3] = (cb[0] + 2 * cb[1]) / 3;
            uint32_t idx = 0;
            for (int i = 0; i < 16; ++i)
            {
                const int r = rgb[i * 3], g = rgb[i * 3 + 1], b = rgb[i * 3 + 2];
                int best = 0, bd = 1 << 30;
                for (int j = 0; j < 4; ++j)
                {
                    const int dr = r - cr[j], dg = g - cg[j], db = b - cb[j];
                    const int d = dr * dr + dg * dg + db * db;
                    if (d < bd) { bd = d; best = j; }
                }
                idx |= uint32_t(best) << (2 * i);
            }
            std::memcpy(out + 4, &idx, 4);
        }

        /**
         * @brief Re-encodes one DXT1 mip level to DXT5, deriving alpha from luminance.
         *
         * Additive FX sprites are commonly authored with no alpha channel at all, relying on a
         * near-black background contributing nothing to the additive blend; when the background
         * isn't quite black it leaves a visible tinted quad. This subtracts a black-level floor
         * and doubles, so dark backgrounds fade toward transparent while the bright sprite stays
         * visible.
         *
         * The client's particle "add" blend (mode 3) maps to device blend (ONE, INVSRCALPHA) --
         * premultiplied alpha, NOT (ONE, ONE): alpha only controls how much of the destination
         * shows through, the source color is always added in full regardless of alpha. So alpha
         * alone does nothing here; the color must be premultiplied by the SAME derived alpha or
         * the background keeps contributing its full unscaled color to the blend.
         */
        std::vector<uint8_t> Dxt1ToDxt5LumaAlpha(const uint8_t* src, uint32_t w, uint32_t h)
        {
            const uint32_t bw = std::max(1u, (w + 3) / 4), bh = std::max(1u, (h + 3) / 4);
            std::vector<uint8_t> out(size_t(bw) * bh * 16);
            for (uint32_t by = 0; by < bh; ++by)
                for (uint32_t bx = 0; bx < bw; ++bx)
                {
                    const uint8_t* block = src + (size_t(by) * bw + bx) * 8;
                    uint8_t rgb[48];
                    DecodeDxt1Block(block, rgb);
                    uint8_t alpha[16];
                    for (int i = 0; i < 16; ++i)
                    {
                        const int lum = (rgb[i * 3] * 77 + rgb[i * 3 + 1] * 151 + rgb[i * 3 + 2] * 28) >> 8;
                        alpha[i] = uint8_t(std::clamp(lum * 2 - 32, 0, 255));
                        for (int c = 0; c < 3; ++c)
                            rgb[i * 3 + c] = uint8_t(rgb[i * 3 + c] * alpha[i] / 255);
                    }
                    uint8_t* o = out.data() + (size_t(by) * bw + bx) * 16;
                    EncodeAlphaBlock(alpha, o);
                    EncodeColorBlock(rgb, o + 8);
                }
            return out;
        }
    }

    bool DdsToBlp(const uint8_t* data, size_t size, std::vector<uint8_t>& out, bool luminanceAlpha)
    {
        if (size < 128 || std::memcmp(data, "DDS ", 4) != 0) return false;
        const uint32_t height = Rd32(data + 12);
        const uint32_t width  = Rd32(data + 16);
        uint32_t mips = Rd32(data + 28);
        if (!mips) mips = 1;
        mips = std::min<uint32_t>(mips, 16);

        uint32_t blockSize;
        uint8_t alphaDepth, alphaType;
        const uint8_t* fourcc = data + 84;
        bool isDxt1 = false;
        if (std::memcmp(fourcc, "DXT1", 4) == 0)      { blockSize = 8;  alphaDepth = 0; alphaType = 0; isDxt1 = true; }
        else if (std::memcmp(fourcc, "DXT3", 4) == 0) { blockSize = 16; alphaDepth = 8; alphaType = 1; }
        else if (std::memcmp(fourcc, "DXT5", 4) == 0) { blockSize = 16; alphaDepth = 8; alphaType = 7; }
        else return false;

        const bool upgradeAlpha = luminanceAlpha && isDxt1;
        if (upgradeAlpha) { blockSize = 16; alphaDepth = 8; alphaType = 7; } // re-encoded as DXT5

        const uint32_t headerSize = 4 + 4 + 4 + 8 + 64 + 64 + 1024; // header + palette block
        uint32_t offsets[16] = {};
        uint32_t sizes[16] = {};
        std::vector<uint8_t> payload;
        size_t src = 128;
        uint32_t w = width, h = height;
        for (uint32_t i = 0; i < mips; ++i)
        {
            const uint32_t srcBlockSize = isDxt1 ? 8 : blockSize;
            const uint32_t n = std::max(1u, (w + 3) / 4) * std::max(1u, (h + 3) / 4) * srcBlockSize;
            if (src + n > size) break;
            offsets[i] = headerSize + uint32_t(payload.size());
            if (upgradeAlpha)
            {
                std::vector<uint8_t> reencoded = Dxt1ToDxt5LumaAlpha(data + src, w, h);
                sizes[i] = uint32_t(reencoded.size());
                payload.insert(payload.end(), reencoded.begin(), reencoded.end());
            }
            else
            {
                sizes[i] = n;
                payload.insert(payload.end(), data + src, data + src + n);
            }
            src += n;
            w = std::max(1u, w / 2);
            h = std::max(1u, h / 2);
        }

        out.clear();
        out.reserve(headerSize + payload.size());
        const auto put32 = [&](uint32_t v) {
            for (int i = 0; i < 4; ++i) out.push_back(uint8_t(v >> (i * 8)));
        };
        out.insert(out.end(), { 'B', 'L', 'P', '2' });
        put32(1);                    // content type
        out.push_back(2);            // block-compressed
        out.push_back(alphaDepth);
        out.push_back(alphaType);
        out.push_back(1);            // has mips
        put32(width);
        put32(height);
        for (uint32_t v : offsets) put32(v);
        for (uint32_t v : sizes) put32(v);
        out.insert(out.end(), 1024, 0); // unused palette block
        out.insert(out.end(), payload.begin(), payload.end());
        return true;
    }
}
