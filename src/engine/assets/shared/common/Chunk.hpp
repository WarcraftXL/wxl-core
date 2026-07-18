// Generic IFF chunk layer: [magic u32 BE][size u32 LE][payload]. No format knowledge; one mechanism
// for walking, finding and emitting chunks, shared by every chunk-based container format (WMO
// today; ADT-style formats fit the same shape).
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
#include <vector>

namespace wxl::modern::assets::common::iff
{
    // Chunk magic packed big-endian to match the stored bytes.
    constexpr uint32_t FourCC(char a, char b, char c, char d)
    {
        return (static_cast<uint32_t>(static_cast<uint8_t>(a)) << 24) |
               (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 16) |
               (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 8) |
                static_cast<uint32_t>(static_cast<uint8_t>(d));
    }

    inline uint16_t Rd16(const uint8_t* p) { return *reinterpret_cast<const uint16_t*>(p); }
    inline uint32_t Rd32(const uint8_t* p) { return *reinterpret_cast<const uint32_t*>(p); }
    inline float    Rdf (const uint8_t* p) { return *reinterpret_cast<const float*>(p); }
    inline void     Wr16(uint8_t* p, int16_t v)  { *reinterpret_cast<int16_t*>(p) = v; }
    inline void     Wr32(uint8_t* p, uint32_t v) { *reinterpret_cast<uint32_t*>(p) = v; }

    // One located chunk. pos/data point PAST the 8-byte header; the full chunk is [data-8, data+size).
    struct Chunk
    {
        uint32_t magic = 0;
        uint32_t pos   = 0;        // payload offset from the span start
        uint32_t size  = 0;        // payload size
        const uint8_t* data = nullptr; // payload pointer, null when absent
    };

    // Forward chunk walker. Stops at the first malformed/truncated header.
    class Reader
    {
    public:
        explicit Reader(std::span<const uint8_t> b)
            : buf_(b.data()), size_(static_cast<uint32_t>(b.size())) {}

        // Visit each chunk in order; f returns false to stop. Returns false if it stopped early.
        template <class F>
        bool ForEach(F&& f) const
        {
            uint32_t p = 0;
            while (p + 8 <= size_)
            {
                const uint32_t clen = 8 + Rd32(buf_ + p + 4);
                if (clen < 8 || p + clen > size_)
                    break;
                Chunk c{ Rd32(buf_ + p), p + 8, clen - 8, buf_ + p + 8 };
                if (!f(c))
                    return false;
                p += clen;
            }
            return true;
        }

        // First chunk with this magic.
        bool Find(uint32_t magic, Chunk& out) const
        {
            bool found = false;
            ForEach([&](const Chunk& c) {
                if (c.magic != magic)
                    return true;
                out = c;
                found = true;
                return false;
            });
            return found;
        }

    private:
        const uint8_t* buf_;
        uint32_t size_;
    };

    // Append a chunk (8-byte header + payload) to out.
    inline void Emit(std::vector<uint8_t>& out, uint32_t magic, const uint8_t* payload, uint32_t len)
    {
        uint8_t hdr[8];
        Wr32(hdr + 0, magic);
        Wr32(hdr + 4, len);
        out.insert(out.end(), hdr, hdr + 8);
        if (len)
            out.insert(out.end(), payload, payload + len);
    }

    // Append a chunk verbatim from a located source chunk (header + payload).
    inline void EmitRaw(std::vector<uint8_t>& out, const Chunk& c)
    {
        out.insert(out.end(), c.data - 8, c.data + c.size);
    }
}
