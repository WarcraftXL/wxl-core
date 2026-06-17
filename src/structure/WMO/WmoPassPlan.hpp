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

// Host->DLL multi-pass plan for one WMO root. The Client draws each surface once; modern materials carry
// extra layers (additive shine/emissive, multiplied lightmap) the down-convert drops. The host records,
// per material that needs an extra layer, the layer texture path + blend, and ships it as a trailing WMP1
// chunk on the root WMO bytes. The DLL reads it before the Client sees the file and hides the chunk by
// shortening the served length, so the native loader never parses it.
namespace wraith::structure::wmo
{
    constexpr uint32_t kWMP1 = iff::FourCC('W', 'M', 'P', '1'); // Wraith multi-pass plan chunk

    // Blend for an extra layer; matches wraith::runtime::multipass::Blend.
    enum class PassBlend : uint8_t { Add = 1, Modulate = 2 };

    // One extra layer on one material (runtime form).
    struct PassPlanEntry
    {
        uint16_t    materialIndex;
        PassBlend   blend;
        std::string texturePath; // layer texture, loadable by the Client texture loader
    };

    // Serialize entries into a WMP1 payload (no chunk header). Layout per entry:
    //   u16 materialIndex, u8 blend, u8 pathLen, char path[pathLen]
    inline void SerializePassPlan(const std::vector<PassPlanEntry>& entries, std::vector<uint8_t>& out)
    {
        out.clear();
        uint8_t hdr[4];
        iff::Wr32(hdr, static_cast<uint32_t>(entries.size()));
        out.insert(out.end(), hdr, hdr + 4);
        for (const PassPlanEntry& e : entries)
        {
            const uint8_t len = static_cast<uint8_t>(e.texturePath.size() > 255 ? 255 : e.texturePath.size());
            uint8_t rec[4];
            rec[0] = static_cast<uint8_t>(e.materialIndex & 0xFF);
            rec[1] = static_cast<uint8_t>(e.materialIndex >> 8);
            rec[2] = static_cast<uint8_t>(e.blend);
            rec[3] = len;
            out.insert(out.end(), rec, rec + 4);
            out.insert(out.end(), e.texturePath.begin(), e.texturePath.begin() + len);
        }
    }

    // Parse a WMP1 payload back into entries. Returns false on a malformed/truncated blob.
    inline bool ParsePassPlan(const uint8_t* data, uint32_t len, std::vector<PassPlanEntry>& out)
    {
        out.clear();
        if (len < 4)
            return false;
        const uint32_t count = iff::Rd32(data);
        uint32_t p = 4;
        for (uint32_t i = 0; i < count; ++i)
        {
            if (p + 4 > len)
                return false;
            PassPlanEntry e;
            e.materialIndex = static_cast<uint16_t>(data[p] | (data[p + 1] << 8));
            e.blend         = static_cast<PassBlend>(data[p + 2]);
            const uint8_t pathLen = data[p + 3];
            p += 4;
            if (p + pathLen > len)
                return false;
            e.texturePath.assign(reinterpret_cast<const char*>(data + p), pathLen);
            p += pathLen;
            out.push_back(std::move(e));
        }
        return true;
    }
}
