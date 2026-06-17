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

#include "WdtFixup.hpp"

#include <cstring>

// Adjust a modern WDT (the tile index) to the flags + MAIN-entry shape the target client (335) reads.
// MPHD flags live at +0x14; the MAIN tile grid (64x64 entries of 8 bytes) starts at +0x3C. The target
// reads the full MPHD flag word and each MAIN entry flag byte without masking, so modern-only high bits
// make it misbehave; they are cleared here. Data-gated: a target-shaped WDT comes out unchanged and is
// served raw.
namespace wraith::structure::wdt
{
    namespace
    {
        constexpr uint32_t CC(char a, char b, char c, char d)
        {
            return (uint32_t(uint8_t(a)) << 24) | (uint32_t(uint8_t(b)) << 16) |
                   (uint32_t(uint8_t(c)) << 8) | uint32_t(uint8_t(d));
        }
        constexpr uint32_t MVER = CC('M','V','E','R');

        uint32_t rd32(const uint8_t* p) { return p[0] | (p[1]<<8) | (p[2]<<16) | (uint32_t(p[3])<<24); }

        constexpr uint32_t kFlagsOffset = 0x14;          // MPHD flags word
        constexpr uint32_t kMainOffset  = 0x3C;          // first MAIN entry
        constexpr uint32_t kMainBytes   = 64u * 64u * 8u; // 64x64 entries, 8 bytes each
    }

    bool FixWdt(std::span<const uint8_t> in, std::vector<uint8_t>& out)
    {
        const uint8_t* b = in.data();
        const uint32_t n = static_cast<uint32_t>(in.size());
        if (n < kMainOffset || rd32(b) != MVER) return false;

        out.assign(in.begin(), in.end());

        uint32_t flags = rd32(out.data() + kFlagsOffset);
        flags &= 0x1F;                    // keep only the bits the target reader understands (drops 0x80)
        for (int i = 0; i < 4; ++i) out[kFlagsOffset + i] = uint8_t(flags >> (i * 8));

        for (size_t i = kMainOffset; i + 8 <= out.size() && i < kMainOffset + kMainBytes; i += 8)
            out[i] &= 0x1;                // keep only has-adt; clear modern/runtime entry bits

        // Target-shaped already (masks were no-ops): serve raw.
        if (out.size() == in.size() && memcmp(out.data(), in.data(), in.size()) == 0)
        {
            out.clear();
            return false;
        }
        return true;
    }
}
