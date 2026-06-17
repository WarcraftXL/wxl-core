// Copyright (C) 2026 WraithEngine
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
#include <string>
#include <vector>

#include "../ChunkIO.hpp"

// Host->DLL terrain texture-scale table. The Client renders all terrain layers at one fixed UV tiling and
// ignores the modern per-texture scale (MTXF/MTXP bits 4-7 = 1<<n). The host records, per texture name,
// that scale exponent, and ships it as a trailing ATSC chunk on the merged .adt bytes. The DLL accumulates
// a global {textureName -> exponent} map and, at terrain draw, multiplies each layer's UV tiling by 1<<n.
// Keyed by texture NAME because the scale is a property of the texture asset (global), not of a tile.
namespace wraith::structure::adt
{
    constexpr uint32_t kATSC = iff::FourCC('A', 'T', 'S', 'C'); // Adt Texture SCale table

    struct TexScaleEntry
    {
        uint8_t     exponent; // UV tiling multiplier = 1 << exponent
        std::string name;     // texture path, as the Client stores it (lowercased on both sides)
    };

    // Serialize into an ATSC payload (no chunk header). Per entry: u8 exponent, u8 nameLen, char name[len].
    inline void SerializeTexScales(const std::vector<TexScaleEntry>& entries, std::vector<uint8_t>& out)
    {
        out.clear();
        uint8_t hdr[4];
        iff::Wr32(hdr, static_cast<uint32_t>(entries.size()));
        out.insert(out.end(), hdr, hdr + 4);
        for (const TexScaleEntry& e : entries)
        {
            const uint8_t len = static_cast<uint8_t>(e.name.size() > 255 ? 255 : e.name.size());
            out.push_back(e.exponent);
            out.push_back(len);
            out.insert(out.end(), e.name.begin(), e.name.begin() + len);
        }
    }

    inline bool ParseTexScales(const uint8_t* data, uint32_t len, std::vector<TexScaleEntry>& out)
    {
        out.clear();
        if (len < 4)
            return false;
        const uint32_t count = iff::Rd32(data);
        uint32_t p = 4;
        for (uint32_t i = 0; i < count; ++i)
        {
            if (p + 2 > len)
                return false;
            TexScaleEntry e;
            e.exponent = data[p];
            const uint8_t nameLen = data[p + 1];
            p += 2;
            if (p + nameLen > len)
                return false;
            e.name.assign(reinterpret_cast<const char*>(data + p), nameLen);
            p += nameLen;
            out.push_back(std::move(e));
        }
        return true;
    }
}
