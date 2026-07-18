// .skel merge: splice a Legion+ split-skeleton file's bones/sequences/attachments back into an MD20 image.
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

#include "Skel.hpp"

#include "Contract.hpp"
#include "common/Log.hpp"

namespace wxl::modern::assets::m2::skel
{
    namespace
    {
        // Chunk names are stored in memory order (not reversed) in the whole M2 family, .skel included.
        constexpr uint32_t Magic(char a, char b, char c, char d)
        {
            return uint32_t(uint8_t(a)) | (uint32_t(uint8_t(b)) << 8) |
                   (uint32_t(uint8_t(c)) << 16) | (uint32_t(uint8_t(d)) << 24);
        }
        constexpr uint32_t kSKS1 = Magic('S', 'K', 'S', '1');
        constexpr uint32_t kSKB1 = Magic('S', 'K', 'B', '1');
        constexpr uint32_t kSKA1 = Magic('S', 'K', 'A', '1');

        // M2Header array-field offsets a .skel chunk can populate.
        constexpr uint32_t kHdrGlobalLoops      = 0x14;
        constexpr uint32_t kHdrSequences        = 0x1C;
        constexpr uint32_t kHdrSequenceLookup   = 0x24;
        constexpr uint32_t kHdrBones            = 0x2C;
        constexpr uint32_t kHdrBoneLookup       = 0x34; // key_bone_lookup
        constexpr uint32_t kHdrAttachments      = 0xF0;
        constexpr uint32_t kHdrAttachmentLookup = 0xF8;

        bool IsArrayEmpty(const uint8_t* md20, uint32_t hdrOff) { return Rd32(md20 + hdrOff) == 0; }

        // Points the M2Array at `hdrOff` at `base + localOff` with `count` entries.
        void SetArray(std::vector<uint8_t>& md20, uint32_t hdrOff, uint32_t count, uint32_t base, uint32_t localOff)
        {
            Wr32(md20.data() + hdrOff,     count);
            Wr32(md20.data() + hdrOff + 4, base + localOff);
        }
    }

    bool Merge(std::span<const uint8_t> skelFile, std::vector<uint8_t>& md20)
    {
        const uint8_t* b = skelFile.data();
        const uint32_t n = static_cast<uint32_t>(skelFile.size());
        bool touched = false;

        for (uint32_t o = 0; o + 8 <= n; )
        {
            const uint32_t tag = Rd32(b + o);
            const uint32_t sz  = Rd32(b + o + 4);
            if (o + 8 + size_t(sz) > n) break;
            const uint8_t* payload = b + o + 8;

            // SKS1 header (32 bytes): global_loops M2Array@0, sequences M2Array@8, sequence_lookups M2Array@16.
            if (tag == kSKS1 && sz >= 32 && IsArrayEmpty(md20.data(), kHdrSequences))
            {
                const uint32_t glC = Rd32(payload +  0), glO = Rd32(payload +  4);
                const uint32_t sqC = Rd32(payload +  8), sqO = Rd32(payload + 12);
                const uint32_t slC = Rd32(payload + 16), slO = Rd32(payload + 20);
                const uint32_t base = static_cast<uint32_t>(md20.size());
                md20.insert(md20.end(), payload, payload + sz);
                SetArray(md20, kHdrGlobalLoops,    glC, base, glO);
                SetArray(md20, kHdrSequences,      sqC, base, sqO);
                SetArray(md20, kHdrSequenceLookup, slC, base, slO);
                touched = true;
            }
            // SKB1 header (16 bytes): bones M2Array@0, key_bone_lookup M2Array@8.
            else if (tag == kSKB1 && sz >= 16 && IsArrayEmpty(md20.data(), kHdrBones))
            {
                const uint32_t bnC = Rd32(payload + 0), bnO = Rd32(payload +  4);
                const uint32_t kbC = Rd32(payload + 8), kbO = Rd32(payload + 12);
                const uint32_t base = static_cast<uint32_t>(md20.size());
                md20.insert(md20.end(), payload, payload + sz);
                SetArray(md20, kHdrBones,      bnC, base, bnO);
                SetArray(md20, kHdrBoneLookup, kbC, base, kbO);
                touched = true;
            }
            // SKA1 header (16 bytes): attachments M2Array@0, attachment_lookup_table M2Array@8.
            else if (tag == kSKA1 && sz >= 16 && IsArrayEmpty(md20.data(), kHdrAttachments))
            {
                const uint32_t atC = Rd32(payload + 0), atO = Rd32(payload +  4);
                const uint32_t alC = Rd32(payload + 8), alO = Rd32(payload + 12);
                const uint32_t base = static_cast<uint32_t>(md20.size());
                md20.insert(md20.end(), payload, payload + sz);
                SetArray(md20, kHdrAttachments,      atC, base, atO);
                SetArray(md20, kHdrAttachmentLookup, alC, base, alO);
                touched = true;
            }

            o += 8 + sz;
        }

        if (touched)
            WLOG_INFO("modern-m2 skel: merged (md20 now %zu bytes)", md20.size());
        return touched;
    }
}
