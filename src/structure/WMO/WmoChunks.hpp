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

#include "Client.hpp"
#include "Source.hpp"

// WMO header policy: how to classify a source and what to do with each chunk magic. The vocabulary
// (magics, layouts) lives in Client.hpp (target) and Source.hpp (modern additions); this file decides
// era and disposition. A new Blizzard chunk is one entry here, not surgery in the transform loop.
namespace wraith::structure::wmo
{
    // WMO era. MVER is 17 in every version, so it cannot tell them apart; detect by chunk presence.
    enum class Era
    {
        Classic,       // 335-: shaders 0..6, classic root + group chunks
        Modern,        // 335+: adds GFID/MODI/MOSI/MOUV, shaders 7..22
        ModernGroupV2, // 9.x+: group rewritten (MOGX/MPY2/MOQG/MOC2/multi-MOTV)
    };

    // Root chunk that proves a modern source.
    constexpr bool IsRootModernMarker(uint32_t magic)
    {
        return magic == kGFID || magic == kMOUV || magic == kMODI || magic == kMOSI;
    }

    // Root chunks the target loader has a slot for; everything else is stripped.
    constexpr bool IsRootKeepChunk(uint32_t magic)
    {
        switch (magic)
        {
            case kMVER: case kMOHD: case kMOTX: case kMOMT: case kMOGN: case kMOGI:
            case kMOSB: case kMOPV: case kMOPT: case kMOPR: case kMOVV: case kMOVB:
            case kMOLT: case kMODS: case kMODN: case kMODD: case kMFOG: case kMCVP:
                return true;
            default:
                return false;
        }
    }

    // Group sub-chunks the target group loader consumes.
    constexpr bool IsKnownGroupChunk(uint32_t magic)
    {
        switch (magic)
        {
            case kMOPY: case kMOVI: case kMOVT: case kMONR: case kMOTV: case kMOBA:
            case kMOLR: case kMODR: case kMOBN: case kMOBR: case kMOCV: case kMLIQ:
                return true;
            default:
                return false;
        }
    }

    // A group is down-convertible when it leads with a known sub-chunk or the newer MOGX/MPY2.
    constexpr bool IsTranslatableGroupFirst(uint32_t magic)
    {
        return IsKnownGroupChunk(magic) || magic == kMOGX || magic == kMPY2;
    }
}
