// WXLC conversion tag: fixed trailing chunk marking a file as WXL-converted, with feature flags.
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
#include <cstring>
#include <vector>

// A converted asset is byte-identical to a native one everywhere that matters to the client parsers,
// so runtime systems cannot tell them apart -- and end up running for every model in the game. This
// trailer is the discriminator: the writer (cold converter, host transforms) appends it to files it
// actually reshaped, and the DLL reads it at load to scope the modern pipelines to tagged instances
// only. An untagged file takes the stock path end to end.
//
// Layout, fixed 16 bytes at end of file (also a well-formed IFF chunk, so a forward chunk walker
// skips it cleanly):
//   [0..3]   'W','X','L','C'
//   [4..7]   u32 payload size = 8
//   [8..11]  u32 feature flags
//   [12..15] u32 reserved = 0
// The tag is additive: offset/header-driven formats (M2, BLP) ignore trailing bytes, chunk-walked
// formats (WMO, ADT) skip an unknown trailing chunk -- the same mechanic as the terrain side tables.
namespace wxl::modern::assets::common::tag
{
    constexpr uint8_t  kMagic[4]     = { 'W', 'X', 'L', 'C' };
    constexpr uint32_t kPayloadSize  = 8;
    constexpr size_t   kTrailerSize  = 16;

    // Feature flags carried by the tag. kConverted is always set on a reshaped file; the rest scope
    // specific runtime pipelines and are reserved until their writers populate them.
    constexpr uint32_t kFlagConverted = 0x1;

    /**
     * @brief Reads the trailing tag of a file image, if present.
     * @param data   file bytes.
     * @param size   file byte count.
     * @param flags  receives the tag's feature flags on success.
     * @return true when the image ends with a well-formed WXLC trailer.
     */
    inline bool Read(const uint8_t* data, size_t size, uint32_t& flags)
    {
        if (!data || size < kTrailerSize) return false;
        const uint8_t* t = data + size - kTrailerSize;
        if (std::memcmp(t, kMagic, sizeof kMagic) != 0) return false;
        uint32_t payload = 0;
        std::memcpy(&payload, t + 4, 4);
        if (payload != kPayloadSize) return false;
        std::memcpy(&flags, t + 8, 4);
        return true;
    }

    /**
     * @brief Appends the tag trailer to a file image (idempotent: an existing trailer is updated).
     * @param bytes  file bytes to tag.
     * @param flags  feature flags to record.
     */
    inline void Append(std::vector<uint8_t>& bytes, uint32_t flags)
    {
        uint32_t existing = 0;
        if (Read(bytes.data(), bytes.size(), existing))
        {
            flags |= existing;
            std::memcpy(bytes.data() + bytes.size() - kTrailerSize + 8, &flags, 4);
            return;
        }
        const size_t base = bytes.size();
        bytes.resize(base + kTrailerSize);
        uint8_t* t = bytes.data() + base;
        std::memcpy(t, kMagic, sizeof kMagic);
        std::memcpy(t + 4, &kPayloadSize, 4);
        std::memcpy(t + 8, &flags, 4);
        std::memset(t + 12, 0, 4);
    }
}
