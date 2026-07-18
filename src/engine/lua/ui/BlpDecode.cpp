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

#include "engine/lua/ui/BlpDecode.hpp"

#include <cstring>

namespace wxl::lua::ui
{
    namespace
    {
        constexpr uint32_t kMagicBlp2  = 0x32504C42; // 'BLP2'
        // BLP2 header field offsets.
        constexpr uint32_t kEncoding   = 0x08; // u8: 1 palettized, 2 DXT, 3 uncompressed BGRA
        constexpr uint32_t kAlphaDepth = 0x09; // u8: 0/1/4/8 bits of alpha
        constexpr uint32_t kAlphaType  = 0x0A; // u8: for DXT, 0=DXT1, 1=DXT3, 7=DXT5
        constexpr uint32_t kWidth      = 0x0C;
        constexpr uint32_t kHeight     = 0x10;
        constexpr uint32_t kMipOffsets = 0x14; // u32[16]
        constexpr uint32_t kMipSizes   = 0x54; // u32[16]
        constexpr uint32_t kPalette    = 0x94; // 256 BGRA entries (palettized only)

        uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }
        uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | (uint16_t(p[1]) << 8)); }
        uint8_t  u8c(int v) { return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v)); }

        void Unpack565(uint16_t c, int& r, int& g, int& b)
        {
            r = ((c >> 11) & 0x1F) << 3; g = ((c >> 5) & 0x3F) << 2; b = (c & 0x1F) << 3;
        }

        // Decodes a DXT color block's four palette entries as R,G,B,A. dxt1Punch selects BC1's 1-bit-alpha
        // mode (index 3 becomes transparent) only when the caller says the block truly carries punch-through.
        void DxtColors(const uint8_t* p, bool dxt1Punch, uint8_t colors[4][4])
        {
            const uint16_t c0 = rd16(p), c1 = rd16(p + 2);
            int r0, g0, b0, r1, g1, b1;
            Unpack565(c0, r0, g0, b0);
            Unpack565(c1, r1, g1, b1);
            colors[0][0] = u8c(r0); colors[0][1] = u8c(g0); colors[0][2] = u8c(b0); colors[0][3] = 255;
            colors[1][0] = u8c(r1); colors[1][1] = u8c(g1); colors[1][2] = u8c(b1); colors[1][3] = 255;
            if (!dxt1Punch || c0 > c1)
            {
                colors[2][0] = u8c((2 * r0 + r1) / 3); colors[2][1] = u8c((2 * g0 + g1) / 3);
                colors[2][2] = u8c((2 * b0 + b1) / 3); colors[2][3] = 255;
                colors[3][0] = u8c((r0 + 2 * r1) / 3); colors[3][1] = u8c((g0 + 2 * g1) / 3);
                colors[3][2] = u8c((b0 + 2 * b1) / 3); colors[3][3] = 255;
            }
            else
            {
                colors[2][0] = u8c((r0 + r1) / 2); colors[2][1] = u8c((g0 + g1) / 2);
                colors[2][2] = u8c((b0 + b1) / 2); colors[2][3] = 255;
                colors[3][0] = colors[3][1] = colors[3][2] = colors[3][3] = 0;
            }
        }

        // encoding 1: an index plane (w*h bytes) into the 256-entry BGRA palette, optionally followed by a
        // separate alpha plane sized by alphaDepth. Emits BGRA.
        bool DecodePalettized(const uint8_t* b, uint32_t n, uint32_t w, uint32_t h, uint8_t alphaDepth,
                              uint32_t off, std::vector<uint8_t>& out)
        {
            const uint64_t pixels = uint64_t(w) * h;
            if (kPalette + 256u * 4u > n) return false;      // palette must be present
            if (off < kPalette || off > n || pixels > n - off) return false; // index plane in range

            uint64_t alphaBytes = 0;
            if (alphaDepth == 1) alphaBytes = (pixels + 7) / 8;
            else if (alphaDepth == 4) alphaBytes = (pixels + 1) / 2;
            else if (alphaDepth == 8) alphaBytes = pixels;
            if (alphaBytes > 0 && (off + pixels > n || alphaBytes > n - off - pixels)) return false;

            const uint8_t* pal = b + kPalette;
            const uint8_t* idx = b + off;
            const uint8_t* ap  = b + off + pixels;
            out.assign(size_t(pixels) * 4, 0);
            for (uint64_t i = 0; i < pixels; ++i)
            {
                const uint8_t pi = idx[i];
                uint8_t a = 255;
                if (alphaDepth == 1)      a = ((ap[i >> 3] >> (i & 7)) & 1) ? 255 : 0;
                else if (alphaDepth == 4) { const uint8_t pk = ap[i >> 1]; a = uint8_t(((i & 1) ? (pk >> 4) : (pk & 0x0F)) * 17); }
                else if (alphaDepth == 8) a = ap[i];
                uint8_t* d = out.data() + i * 4;
                d[0] = pal[pi * 4 + 0]; d[1] = pal[pi * 4 + 1]; d[2] = pal[pi * 4 + 2]; d[3] = a; // BGRA
            }
            return true;
        }

        // encoding 2: DXT1/DXT3/DXT5 4x4 blocks. Emits BGRA.
        bool DecodeDxt(const uint8_t* b, uint32_t n, uint32_t w, uint32_t h, uint8_t alphaType,
                       uint8_t alphaDepth, uint32_t off, uint32_t size, std::vector<uint8_t>& out)
        {
            const uint32_t blockBytes = (alphaType == 0) ? 8u : 16u;
            const uint32_t bx = (w + 3) / 4, by = (h + 3) / 4;
            if (off > n || size > n - off) return false;
            if (uint64_t(bx) * by * blockBytes > size) return false;

            out.assign(size_t(w) * h * 4, 0);
            const uint8_t* src = b + off;
            for (uint32_t ty = 0; ty < by; ++ty)
                for (uint32_t tx = 0; tx < bx; ++tx)
                {
                    const uint8_t* block = src + (size_t(ty) * bx + tx) * blockBytes;
                    uint8_t alpha[16];
                    for (int i = 0; i < 16; ++i) alpha[i] = 255;
                    const uint8_t* colorBlock = block;
                    if (alphaType == 1) // DXT3: 4-bit explicit alpha
                    {
                        for (int i = 0; i < 16; ++i)
                        {
                            const uint8_t pk = block[i / 2];
                            alpha[i] = uint8_t(((i & 1) ? (pk >> 4) : (pk & 0x0F)) * 17);
                        }
                        colorBlock = block + 8;
                    }
                    else if (alphaType == 7) // DXT5: interpolated alpha
                    {
                        uint8_t al[8];
                        al[0] = block[0]; al[1] = block[1];
                        if (al[0] > al[1])
                            for (int i = 1; i < 7; ++i) al[i + 1] = uint8_t(((7 - i) * al[0] + i * al[1]) / 7);
                        else
                        {
                            for (int i = 1; i < 5; ++i) al[i + 1] = uint8_t(((5 - i) * al[0] + i * al[1]) / 5);
                            al[6] = 0; al[7] = 255;
                        }
                        uint64_t bits = 0;
                        for (int i = 0; i < 6; ++i) bits |= uint64_t(block[2 + i]) << (8 * i);
                        for (int i = 0; i < 16; ++i) alpha[i] = al[(bits >> (3 * i)) & 7];
                        colorBlock = block + 8;
                    }

                    uint8_t colors[4][4];
                    DxtColors(colorBlock, alphaType == 0 && alphaDepth != 0, colors);
                    const uint32_t cidx = rd32(colorBlock + 4);
                    for (uint32_t py = 0; py < 4; ++py)
                        for (uint32_t px = 0; px < 4; ++px)
                        {
                            const uint32_t x = tx * 4 + px, y = ty * 4 + py;
                            if (x >= w || y >= h) continue;
                            const uint32_t si = py * 4 + px;
                            const uint32_t ci = (cidx >> (2 * si)) & 3;
                            uint8_t* d = out.data() + (size_t(y) * w + x) * 4;
                            d[0] = colors[ci][2]; d[1] = colors[ci][1]; d[2] = colors[ci][0]; // RGB -> BGR
                            d[3] = (alphaType == 0 && colors[ci][3] == 0) ? 0 : alpha[si];
                        }
                }
            return true;
        }

        // encoding 3: mip 0 is already tightly packed BGRA; copy verbatim.
        bool DecodeRawBgra(const uint8_t* b, uint32_t n, uint32_t w, uint32_t h, uint32_t off,
                           uint32_t size, std::vector<uint8_t>& out)
        {
            const uint64_t need = uint64_t(w) * h * 4;
            if (off > n || size < need || need > n - off) return false;
            out.assign(size_t(need), 0);
            std::memcpy(out.data(), b + off, size_t(need));
            return true;
        }
    } // namespace

    bool DecodeBlpToBgra(const uint8_t* data, size_t size, int& width, int& height,
                         std::vector<uint8_t>& outBgra)
    {
        outBgra.clear();
        if (!data || size < kPalette) return false;
        const uint8_t* b = data;
        const uint32_t n = static_cast<uint32_t>(size);
        if (rd32(b) != kMagicBlp2 || rd32(b + 4) != 1) return false; // BLP2 only (BLP1/JPEG-BLP unsupported)

        const uint8_t  enc = b[kEncoding], alphaDepth = b[kAlphaDepth], alphaType = b[kAlphaType];
        const uint32_t w = rd32(b + kWidth), h = rd32(b + kHeight);
        if (w == 0 || h == 0 || w > 4096 || h > 4096) return false;

        const uint32_t off = rd32(b + kMipOffsets), sz = rd32(b + kMipSizes);
        if (off == 0 || sz == 0) return false;

        bool ok = false;
        if (enc == 1)
            ok = DecodePalettized(b, n, w, h, alphaDepth, off, outBgra);
        else if (enc == 2 && (alphaType == 0 || alphaType == 1 || alphaType == 7))
            ok = DecodeDxt(b, n, w, h, alphaType, alphaDepth, off, sz, outBgra);
        else if (enc == 3)
            ok = DecodeRawBgra(b, n, w, h, off, sz, outBgra);

        if (!ok) { outBgra.clear(); return false; }
        width = static_cast<int>(w);
        height = static_cast<int>(h);
        return true;
    }
}
