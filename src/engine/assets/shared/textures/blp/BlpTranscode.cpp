// BLP transcode: re-encode an uncompressed-BGRA (encoding 3) image to DXT5 the Client decodes.
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

#include "BlpTranscode.hpp"

#include "Dxt.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace wxl::modern::assets::textures::blp
{
    namespace
    {
        constexpr uint32_t kMagicBlp2 = 0x32504C42; // 'BLP2'

        uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }
        void     wr32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = uint8_t(v >> (i * 8)); }
        uint32_t mx(uint32_t a, uint32_t b) { return a > b ? a : b; }

        // BLP2 header field offsets.
        constexpr uint32_t kEncoding    = 0x08; // u8: 1 palettized, 2 DXT, 3 uncompressed BGRA
        constexpr uint32_t kAlphaDepth  = 0x09; // u8
        constexpr uint32_t kAlphaType   = 0x0A; // u8: 0 DXT1, 1 DXT3, 7 DXT5
        constexpr uint32_t kWidth       = 0x0C;
        constexpr uint32_t kHeight      = 0x10;
        constexpr uint32_t kMipOffsets  = 0x14; // u32[16]
        constexpr uint32_t kMipSizes    = 0x54; // u32[16]

        struct RgbaImage
        {
            uint32_t w = 0;
            uint32_t h = 0;
            std::vector<uint8_t> px; // RGBA
        };

        struct PaletteColor
        {
            uint8_t r = 0;
            uint8_t g = 0;
            uint8_t b = 0;
            uint32_t count = 0;
        };

        struct PaletteBox
        {
            size_t first = 0;
            size_t last = 0;
        };

        uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | (uint16_t(p[1]) << 8)); }
        uint8_t  u8(int v) { return static_cast<uint8_t>(std::clamp(v, 0, 255)); }

        void DecodeDxtColors(const uint8_t* p, bool dxt1Alpha, uint8_t colors[4][4])
        {
            const uint16_t c0 = rd16(p), c1 = rd16(p + 2);
            int r0, g0, b0, r1, g1, b1;
            dxt::Unpack565(c0, r0, g0, b0);
            dxt::Unpack565(c1, r1, g1, b1);

            colors[0][0] = u8(r0); colors[0][1] = u8(g0); colors[0][2] = u8(b0); colors[0][3] = 255;
            colors[1][0] = u8(r1); colors[1][1] = u8(g1); colors[1][2] = u8(b1); colors[1][3] = 255;
            if (!dxt1Alpha || c0 > c1)
            {
                colors[2][0] = u8((2 * r0 + r1) / 3);
                colors[2][1] = u8((2 * g0 + g1) / 3);
                colors[2][2] = u8((2 * b0 + b1) / 3);
                colors[2][3] = 255;
                colors[3][0] = u8((r0 + 2 * r1) / 3);
                colors[3][1] = u8((g0 + 2 * g1) / 3);
                colors[3][2] = u8((b0 + 2 * b1) / 3);
                colors[3][3] = 255;
            }
            else
            {
                colors[2][0] = u8((r0 + r1) / 2);
                colors[2][1] = u8((g0 + g1) / 2);
                colors[2][2] = u8((b0 + b1) / 2);
                colors[2][3] = 255;
                colors[3][0] = colors[3][1] = colors[3][2] = colors[3][3] = 0;
            }
        }

        bool DecodeDxtMip(std::span<const uint8_t> in, RgbaImage& img, uint32_t maxEdge)
        {
            const uint8_t* b = in.data();
            const uint32_t n = static_cast<uint32_t>(in.size());
            if (n < 0x94 || rd32(b) != kMagicBlp2 || rd32(b + 4) != 1) return false;
            if (b[kEncoding] != 2) return false;

            const uint8_t alphaDepth = b[kAlphaDepth];
            const uint8_t alphaType = b[kAlphaType];
            if (alphaType != 0 && alphaType != 1 && alphaType != 7) return false;

            const uint32_t width = rd32(b + kWidth), height = rd32(b + kHeight);
            if (width == 0 || height == 0 || width > 4096 || height > 4096) return false;

            uint32_t mip = 0;
            if (maxEdge != 0)
                while (mip < 15 && mx(mx(1u, width >> mip), mx(1u, height >> mip)) > maxEdge) ++mip;

            uint32_t off = rd32(b + kMipOffsets + mip * 4), size = rd32(b + kMipSizes + mip * 4);
            if ((off == 0 || size == 0 || off > n || size > n - off) && mip != 0)
            {
                mip = 0;
                off = rd32(b + kMipOffsets);
                size = rd32(b + kMipSizes);
            }
            if (off == 0 || size == 0 || off > n || size > n - off) return false;

            const uint32_t mipW = mx(1u, width >> mip), mipH = mx(1u, height >> mip);
            const uint32_t blockBytes = (alphaType == 0) ? 8u : 16u;
            const uint32_t bx = (mipW + 3) / 4, by = (mipH + 3) / 4;
            if (uint64_t(bx) * by * blockBytes > size) return false;

            img.w = mipW;
            img.h = mipH;
            img.px.assign(size_t(mipW) * mipH * 4, 0);

            const uint8_t* src = b + off;
            for (uint32_t ty = 0; ty < by; ++ty)
            {
                for (uint32_t tx = 0; tx < bx; ++tx)
                {
                    const uint8_t* block = src + (size_t(ty) * bx + tx) * blockBytes;
                    uint8_t alpha[16];
                    std::fill(std::begin(alpha), std::end(alpha), uint8_t(255));

                    const uint8_t* colorBlock = block;
                    if (alphaType == 1)
                    {
                        for (int i = 0; i < 16; ++i)
                        {
                            const uint8_t packed = block[i / 2];
                            const uint8_t nibble = (i & 1) ? (packed >> 4) : (packed & 0x0f);
                            alpha[i] = uint8_t(nibble * 17);
                        }
                        colorBlock = block + 8;
                    }
                    else if (alphaType == 7)
                    {
                        uint8_t al[8];
                        al[0] = block[0]; al[1] = block[1];
                        if (al[0] > al[1])
                        {
                            for (int i = 1; i < 7; ++i)
                                al[i + 1] = uint8_t(((7 - i) * al[0] + i * al[1]) / 7);
                        }
                        else
                        {
                            for (int i = 1; i < 5; ++i)
                                al[i + 1] = uint8_t(((5 - i) * al[0] + i * al[1]) / 5);
                            al[6] = 0;
                            al[7] = 255;
                        }

                        uint64_t bits = 0;
                        for (int i = 0; i < 6; ++i) bits |= uint64_t(block[2 + i]) << (8 * i);
                        for (int i = 0; i < 16; ++i)
                            alpha[i] = al[(bits >> (3 * i)) & 7];
                        colorBlock = block + 8;
                    }

                    uint8_t colors[4][4];
                    // BLP uses alphaDepth to distinguish opaque BC1/DXT1 from its 1-bit-alpha mode.
                    // Endpoint ordering alone is insufficient: opaque blocks are allowed to have c0 <= c1,
                    // where index 3 must remain an opaque interpolated color instead of becoming transparent.
                    DecodeDxtColors(colorBlock, alphaType == 0 && alphaDepth != 0, colors);
                    const uint32_t cidx = rd32(colorBlock + 4);
                    for (uint32_t py = 0; py < 4; ++py)
                    {
                        for (uint32_t px = 0; px < 4; ++px)
                        {
                            const uint32_t x = tx * 4 + px, y = ty * 4 + py;
                            if (x >= mipW || y >= mipH) continue;
                            const uint32_t si = py * 4 + px;
                            const uint32_t ci = (cidx >> (2 * si)) & 3;
                            uint8_t* dst = img.px.data() + (size_t(y) * mipW + x) * 4;
                            dst[0] = colors[ci][0];
                            dst[1] = colors[ci][1];
                            dst[2] = colors[ci][2];
                            dst[3] = (alphaType == 0 && colors[ci][3] == 0) ? 0 : alpha[si];
                        }
                    }
                }
            }
            return true;
        }

        RgbaImage DownsampleHalf(const RgbaImage& src)
        {
            RgbaImage dst;
            dst.w = std::max(1u, src.w / 2);
            dst.h = std::max(1u, src.h / 2);
            dst.px.assign(size_t(dst.w) * dst.h * 4, 0);
            for (uint32_t y = 0; y < dst.h; ++y)
            {
                for (uint32_t x = 0; x < dst.w; ++x)
                {
                    uint32_t sum[4] = {};
                    uint32_t count = 0;
                    for (uint32_t oy = 0; oy < 2; ++oy)
                    {
                        for (uint32_t ox = 0; ox < 2; ++ox)
                        {
                            const uint32_t sx = std::min(src.w - 1, x * 2 + ox);
                            const uint32_t sy = std::min(src.h - 1, y * 2 + oy);
                            const uint8_t* sp = src.px.data() + (size_t(sy) * src.w + sx) * 4;
                            for (int k = 0; k < 4; ++k) sum[k] += sp[k];
                            ++count;
                        }
                    }
                    uint8_t* dp = dst.px.data() + (size_t(y) * dst.w + x) * 4;
                    for (int k = 0; k < 4; ++k) dp[k] = uint8_t(sum[k] / count);
                }
            }
            return dst;
        }

        uint16_t ColorKey(uint8_t r, uint8_t g, uint8_t b)
        {
            return uint16_t(((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
        }

        PaletteColor KeyCenter(uint16_t key, uint32_t count)
        {
            const uint8_t r5 = uint8_t((key >> 10) & 31);
            const uint8_t g5 = uint8_t((key >> 5) & 31);
            const uint8_t b5 = uint8_t(key & 31);
            return PaletteColor{
                uint8_t((uint32_t(r5) * 255 + 15) / 31),
                uint8_t((uint32_t(g5) * 255 + 15) / 31),
                uint8_t((uint32_t(b5) * 255 + 15) / 31),
                count
            };
        }

        void BoxBounds(const std::vector<PaletteColor>& colors, const PaletteBox& box,
                       uint8_t& rMin, uint8_t& rMax, uint8_t& gMin, uint8_t& gMax,
                       uint8_t& bMin, uint8_t& bMax, uint64_t& total)
        {
            rMin = gMin = bMin = 255;
            rMax = gMax = bMax = 0;
            total = 0;
            for (size_t i = box.first; i < box.last; ++i)
            {
                const PaletteColor& c = colors[i];
                rMin = std::min(rMin, c.r); rMax = std::max(rMax, c.r);
                gMin = std::min(gMin, c.g); gMax = std::max(gMax, c.g);
                bMin = std::min(bMin, c.b); bMax = std::max(bMax, c.b);
                total += c.count;
            }
        }

        std::array<std::array<uint8_t, 3>, 256> BuildPalette(const RgbaImage& base)
        {
            std::array<uint32_t, 32768> hist{};
            const size_t pixels = size_t(base.w) * base.h;
            for (size_t i = 0; i < pixels; ++i)
            {
                const uint8_t* p = base.px.data() + i * 4;
                if (p[3] < 8) continue;
                ++hist[ColorKey(p[0], p[1], p[2])];
            }

            std::vector<PaletteColor> colors;
            colors.reserve(32768);
            for (uint32_t i = 0; i < hist.size(); ++i)
                if (hist[i] != 0)
                    colors.push_back(KeyCenter(static_cast<uint16_t>(i), hist[i]));

            if (colors.empty())
                colors.push_back(PaletteColor{ 0, 0, 0, 1 });

            std::vector<PaletteBox> boxes;
            boxes.push_back(PaletteBox{ 0, colors.size() });
            while (boxes.size() < 256)
            {
                size_t best = boxes.size();
                uint32_t bestScore = 0;
                uint8_t brMin = 0, brMax = 0, bgMin = 0, bgMax = 0, bbMin = 0, bbMax = 0;
                uint64_t bestTotal = 0;

                for (size_t i = 0; i < boxes.size(); ++i)
                {
                    const PaletteBox& box = boxes[i];
                    if (box.last - box.first <= 1) continue;
                    uint8_t rMin, rMax, gMin, gMax, bMin, bMax;
                    uint64_t total;
                    BoxBounds(colors, box, rMin, rMax, gMin, gMax, bMin, bMax, total);
                    const uint32_t rRange = rMax - rMin;
                    const uint32_t gRange = gMax - gMin;
                    const uint32_t bRange = bMax - bMin;
                    const uint32_t score = std::max({ rRange, gRange, bRange }) * static_cast<uint32_t>(std::min<uint64_t>(total, 0xffff));
                    if (score > bestScore)
                    {
                        best = i;
                        bestScore = score;
                        brMin = rMin; brMax = rMax;
                        bgMin = gMin; bgMax = gMax;
                        bbMin = bMin; bbMax = bMax;
                        bestTotal = total;
                    }
                }

                if (best == boxes.size()) break;

                PaletteBox box = boxes[best];
                const uint32_t rRange = brMax - brMin;
                const uint32_t gRange = bgMax - bgMin;
                const uint32_t bRange = bbMax - bbMin;
                if (rRange >= gRange && rRange >= bRange)
                    std::sort(colors.begin() + box.first, colors.begin() + box.last,
                              [](const PaletteColor& a, const PaletteColor& b) { return a.r < b.r; });
                else if (gRange >= bRange)
                    std::sort(colors.begin() + box.first, colors.begin() + box.last,
                              [](const PaletteColor& a, const PaletteColor& b) { return a.g < b.g; });
                else
                    std::sort(colors.begin() + box.first, colors.begin() + box.last,
                              [](const PaletteColor& a, const PaletteColor& b) { return a.b < b.b; });

                uint64_t leftCount = 0;
                size_t split = box.first + 1;
                const uint64_t half = std::max<uint64_t>(1, bestTotal / 2);
                for (; split < box.last; ++split)
                {
                    leftCount += colors[split - 1].count;
                    if (leftCount >= half) break;
                }
                split = std::clamp(split, box.first + 1, box.last - 1);

                boxes[best] = PaletteBox{ box.first, split };
                boxes.push_back(PaletteBox{ split, box.last });
            }

            std::array<std::array<uint8_t, 3>, 256> palette{};
            size_t pi = 0;
            for (const PaletteBox& box : boxes)
            {
                uint64_t total = 0, r = 0, g = 0, b = 0;
                for (size_t i = box.first; i < box.last; ++i)
                {
                    const PaletteColor& c = colors[i];
                    total += c.count;
                    r += uint64_t(c.r) * c.count;
                    g += uint64_t(c.g) * c.count;
                    b += uint64_t(c.b) * c.count;
                }
                if (total == 0) total = 1;
                palette[pi++] = {
                    uint8_t(r / total),
                    uint8_t(g / total),
                    uint8_t(b / total)
                };
                if (pi == palette.size()) break;
            }
            while (pi < palette.size())
            {
                palette[pi] = palette[pi ? pi - 1 : 0];
                ++pi;
            }
            return palette;
        }

        std::array<uint8_t, 32768> BuildPaletteMap(const std::array<std::array<uint8_t, 3>, 256>& palette)
        {
            std::array<uint8_t, 32768> map{};
            for (uint32_t key = 0; key < map.size(); ++key)
            {
                const PaletteColor c = KeyCenter(static_cast<uint16_t>(key), 1);
                uint32_t bestDist = 0xffffffffu;
                uint8_t best = 0;
                for (uint32_t i = 0; i < palette.size(); ++i)
                {
                    const int dr = int(c.r) - palette[i][0];
                    const int dg = int(c.g) - palette[i][1];
                    const int db = int(c.b) - palette[i][2];
                    const uint32_t dist = uint32_t(dr * dr + dg * dg + db * db);
                    if (dist < bestDist)
                    {
                        bestDist = dist;
                        best = static_cast<uint8_t>(i);
                    }
                }
                map[key] = best;
            }
            return map;
        }

        void WritePalette(uint8_t* dst, const std::array<std::array<uint8_t, 3>, 256>& palette)
        {
            for (size_t i = 0; i < palette.size(); ++i)
            {
                dst[i * 4 + 0] = palette[i][2];
                dst[i * 4 + 1] = palette[i][1];
                dst[i * 4 + 2] = palette[i][0];
                dst[i * 4 + 3] = 255;
            }
        }

        bool EncodePalettedBlp(const RgbaImage& base, std::vector<uint8_t>& out)
        {
            if (base.w == 0 || base.h == 0 || base.px.size() != size_t(base.w) * base.h * 4) return false;

            std::vector<RgbaImage> mips;
            mips.push_back(base);
            while ((mips.back().w > 1 || mips.back().h > 1) && mips.size() < 16)
                mips.push_back(DownsampleHalf(mips.back()));

            out.assign(0x94 + 1024, 0);
            wr32(out.data(), kMagicBlp2);
            wr32(out.data() + 4, 1);
            out[kEncoding] = 1;
            out[kAlphaDepth] = 8;
            out[kAlphaType] = 8;
            out[0x0b] = 1;
            wr32(out.data() + kWidth, base.w);
            wr32(out.data() + kHeight, base.h);
            const auto palette = BuildPalette(base);
            const auto paletteMap = BuildPaletteMap(palette);
            WritePalette(out.data() + 0x94, palette);

            uint32_t offsets[16] = {}, sizes[16] = {};
            for (size_t mip = 0; mip < mips.size(); ++mip)
            {
                const RgbaImage& img = mips[mip];
                offsets[mip] = static_cast<uint32_t>(out.size());
                const size_t pixels = size_t(img.w) * img.h;
                for (size_t i = 0; i < pixels; ++i)
                {
                    const uint8_t* p = img.px.data() + i * 4;
                    out.push_back(paletteMap[ColorKey(p[0], p[1], p[2])]);
                }
                for (size_t i = 0; i < pixels; ++i)
                    out.push_back(img.px[i * 4 + 3]);
                sizes[mip] = static_cast<uint32_t>(out.size()) - offsets[mip];
            }
            for (int i = 0; i < 16; ++i)
            {
                wr32(out.data() + kMipOffsets + i * 4, offsets[i]);
                wr32(out.data() + kMipSizes + i * 4, sizes[i]);
            }
            return true;
        }
    }

    bool CapBlpMips(std::span<const uint8_t> in, std::vector<uint8_t>& out, uint32_t maxEdge)
    {
        const uint8_t* b = in.data();
        const uint32_t n = static_cast<uint32_t>(in.size());
        // BLP1 has a different header layout. Treating its compression field as the BLP2 version and
        // reading BLP2 mip offsets can walk outside the source buffer (several legacy Stormwind textures
        // use BLP1), so the generic mip dropper must be strictly BLP2-only.
        if (n < 0x94 || rd32(b) != kMagicBlp2 || rd32(b + 4) != 1) return false;

        const uint32_t width = rd32(b + kWidth), height = rd32(b + kHeight);
        if (width == 0 || height == 0 || width > 16384 || height > 16384 || maxEdge == 0) return false;
        const uint32_t maxdim = mx(width, height);
        if (maxdim <= maxEdge) return false; // already within budget

        uint32_t drop = 0;
        while (drop < 15 && (maxdim >> drop) > maxEdge) ++drop; // halvings until the larger edge fits
        if ((maxdim >> drop) > maxEdge) return false;

        uint32_t mipOff[16], mipSize[16];
        for (int i = 0; i < 16; ++i) { mipOff[i] = rd32(b + kMipOffsets + i * 4); mipSize[i] = rd32(b + kMipSizes + i * 4); }
        // The target level must exist in the chain, and the data region must be in range.
        if (drop >= 16 || mipSize[drop] == 0 || mipOff[drop] == 0) return false;
        if (mipOff[0] < 0x94 || mipOff[0] > n) return false;

        // Header (and palette for the palettized encoding) live before the first mip; keep them verbatim.
        const uint32_t dataStart = mipOff[0];
        out.assign(b, b + dataStart);

        uint32_t newOff[16] = {}, newSize[16] = {};
        bool copiedAny = false;
        for (uint32_t i = drop, j = 0; i < 16; ++i, ++j)
        {
            if (mipSize[i] == 0 || mipOff[i] == 0) break;
            if (mipOff[i] > n || mipSize[i] > n - mipOff[i]) break; // truncated/overflowed level
            newOff[j]  = static_cast<uint32_t>(out.size());
            out.insert(out.end(), b + mipOff[i], b + mipOff[i] + mipSize[i]);
            newSize[j] = mipSize[i];
            copiedAny = true;
        }

        if (!copiedAny) { out.clear(); return false; }

        wr32(out.data() + kWidth,  mx(1u, width  >> drop));
        wr32(out.data() + kHeight, mx(1u, height >> drop));
        for (int i = 0; i < 16; ++i) { wr32(out.data() + kMipOffsets + i * 4, newOff[i]); wr32(out.data() + kMipSizes + i * 4, newSize[i]); }
        return true;
    }

    bool TranscodeBlp(std::span<const uint8_t> in, std::vector<uint8_t>& out)
    {
        const uint8_t* b = in.data();
        const uint32_t n = static_cast<uint32_t>(in.size());
        if (n < 0x94 || rd32(b) != kMagicBlp2 || rd32(b + 4) != 1) return false; // not a BLP image
        if (b[kEncoding] != 3) return false;                                      // only uncompressed BGRA

        const uint32_t width = rd32(b + kWidth), height = rd32(b + kHeight);
        const uint8_t  srcAlpha = b[kAlphaDepth];
        if (width == 0 || height == 0) return false;

        uint32_t mipOff[16], mipSize[16];
        for (int i = 0; i < 16; ++i) { mipOff[i] = rd32(b + kMipOffsets + i * 4); mipSize[i] = rd32(b + kMipSizes + i * 4); }
        if (mipSize[0] == 0 || mipOff[0] == 0 || mipOff[0] > n) return false;

        // Bytes per source pixel from mip 0 (encoding 3 is normally 4-byte BGRA; tolerate 3-byte BGR).
        const uint64_t px0 = uint64_t(width) * height;
        const uint32_t bpp = static_cast<uint32_t>(mipSize[0] / px0);
        if (bpp != 3 && bpp != 4) return false;

        // Keep the source header + palette region verbatim, retag it as DXT5, then append the re-encoded mips.
        out.assign(b, b + mipOff[0]);
        out[kEncoding]   = 2; // DXT
        out[kAlphaDepth] = 8;
        out[kAlphaType]  = 7; // DXT5

        uint32_t newOff[16] = {}, newSize[16] = {};
        for (int i = 0; i < 16; ++i)
        {
            if (mipSize[i] == 0 || mipOff[i] == 0) continue;
            const uint32_t mw = mx(1u, width >> i), mh = mx(1u, height >> i);
            if (mipOff[i] + size_t(mw) * mh * bpp > n) break; // truncated source mip
            const uint8_t* src = b + mipOff[i];

            newOff[i] = static_cast<uint32_t>(out.size());
            const uint32_t bx = (mw + 3) / 4, by = (mh + 3) / 4;
            for (uint32_t ty = 0; ty < by; ++ty)
                for (uint32_t tx = 0; tx < bx; ++tx)
                {
                    uint8_t block[64];
                    for (uint32_t py = 0; py < 4; ++py)
                        for (uint32_t pxi = 0; pxi < 4; ++pxi)
                        {
                            const uint32_t sx = (tx * 4 + pxi < mw) ? tx * 4 + pxi : mw - 1; // clamp edge
                            const uint32_t sy = (ty * 4 + py  < mh) ? ty * 4 + py  : mh - 1;
                            const uint8_t* p = src + (size_t(sy) * mw + sx) * bpp;
                            uint8_t* d = block + (py * 4 + pxi) * 4;
                            d[0] = p[2]; d[1] = p[1]; d[2] = p[0];               // BGR -> RGB
                            d[3] = (bpp == 4 && srcAlpha != 0) ? p[3] : 255;     // opaque when no source alpha
                        }
                    uint8_t enc[16];
                    dxt::CompressBlock(block, enc);
                    out.insert(out.end(), enc, enc + 16);
                }
            newSize[i] = static_cast<uint32_t>(out.size()) - newOff[i];
        }

        for (int i = 0; i < 16; ++i)
        {
            wr32(out.data() + kMipOffsets + i * 4, newOff[i]);
            wr32(out.data() + kMipSizes + i * 4, newSize[i]);
        }
        return true;
    }

    bool TextureComponentToPaletted(std::span<const uint8_t> in, std::vector<uint8_t>& out,
                                    uint32_t maxEdge)
    {
        // Native character component textures usually top out around 256px on the long edge. Higher
        // componentTextureLevel values can ask the host to preserve a larger modern mip before palettizing.
        if (maxEdge < 256) maxEdge = 256;
        if (maxEdge > 1024) maxEdge = 1024;

        RgbaImage img;
        if (!DecodeDxtMip(in, img, maxEdge)) return false;

        return EncodePalettedBlp(img, out);
    }
}
