// modern-anim shared transform: normalize external M2 animation files for the native loader.
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

#include "ModernAnim.hpp"

#include "structure/m2/M2Format.hpp"

#include <cstddef>
#include <cctype>
#include <cstring>

namespace wxl::scripts::modernanim
{
    namespace fmt = wxl::structure::m2;

    namespace
    {
        constexpr uint32_t kSourceVersionMin = 272;
        constexpr uint32_t kSourceVersionMax = 274;
        constexpr uint32_t kStagedVersionBit = 0x40000000;
        constexpr uint16_t kClientMaxAnimId = 505;
        constexpr uint32_t kSequenceHeaderSize = offsetof(fmt::M2Header, bones);

        constexpr uint32_t Magic(char a, char b, char c, char d)
        {
            return uint32_t(uint8_t(a)) | (uint32_t(uint8_t(b)) << 8) |
                   (uint32_t(uint8_t(c)) << 16) | (uint32_t(uint8_t(d)) << 24);
        }
        constexpr uint32_t kMagicAFM2 = Magic('A', 'F', 'M', '2');
        constexpr uint32_t kMagicAFSB = Magic('A', 'F', 'S', 'B');

        bool IsStagedVersion(uint32_t v) { return (v & kStagedVersionBit) != 0; }

        bool HasSourceVersion(uint32_t version)
        {
            return (version >= kSourceVersionMin && version <= kSourceVersionMax) ||
                   IsStagedVersion(version);
        }

        bool EndsWithCI(std::string_view value, std::string_view suffix)
        {
            if (suffix.size() > value.size()) return false;
            const size_t off = value.size() - suffix.size();
            for (size_t i = 0; i < suffix.size(); ++i)
            {
                const auto a = static_cast<unsigned char>(value[off + i]);
                const auto b = static_cast<unsigned char>(suffix[i]);
                if (std::tolower(a) != std::tolower(b)) return false;
            }
            return true;
        }

        bool FitsArray(uint32_t offset, uint32_t count, uint32_t stride, uint32_t size)
        {
            if (!count) return true;
            if (!offset || offset > size) return false;
            return count <= (size - offset) / stride;
        }

        uint32_t Rd32(const uint8_t* p)
        {
            uint32_t v = 0;
            std::memcpy(&v, p, sizeof(v));
            return v;
        }

        bool IsWrappedAnim(uint32_t magic)
        {
            return magic == kMagicAFM2 || magic == kMagicAFSB;
        }

        int16_t AnimationIndex(const fmt::M2Sequence* seqs, uint32_t count, uint16_t anim)
        {
            for (uint32_t i = 0; i < count; ++i)
                if (seqs[i].id == anim) return static_cast<int16_t>(i);
            return -1;
        }

        void ReplaceAnimLookup(int16_t* lookup, uint32_t lookupCount, int16_t oldPos, uint16_t newId,
                               int16_t newPos)
        {
            if (newId < lookupCount && lookup[newId] == oldPos) { lookup[newId] = newPos; return; }
            for (uint32_t i = 0; i < lookupCount; ++i)
                if (lookup[i] == oldPos) { lookup[i] = newPos; break; }
        }

        uint16_t RemappedAnimId(uint16_t id)
        {
            switch (id)
            {
                case 564: return 37;
                case 548: return 41;
                case 556: return 42;
                case 552: return 43;
                case 554: return 44;
                case 562: return 45;
                case 572: return 39;
                case 574: return 187;
                default:  return id;
            }
        }

        void FixSequences(fmt::M2Header* md, uint32_t size)
        {
            if (!FitsArray(md->sequences.offset, md->sequences.count, sizeof(fmt::M2Sequence), size))
                return;
            if (!md->sequences.count || !md->sequences.offset) return;

            auto* seqs = reinterpret_cast<fmt::M2Sequence*>(md->base() + md->sequences.offset);
            const uint32_t count = md->sequences.count;

            int16_t* lookup = nullptr;
            uint32_t lookupCount = 0;
            if (md->sequenceLookup.count && md->sequenceLookup.offset &&
                FitsArray(md->sequenceLookup.offset, md->sequenceLookup.count, sizeof(int16_t), size))
            {
                lookup = reinterpret_cast<int16_t*>(md->base() + md->sequenceLookup.offset);
                lookupCount = md->sequenceLookup.count;
            }

            for (uint32_t i = 0; i < count; ++i)
            {
                const uint16_t id = seqs[i].id;
                if (id > kClientMaxAnimId)
                {
                    const uint16_t anim = RemappedAnimId(id);
                    if (id != anim)
                    {
                        if (lookup)
                            ReplaceAnimLookup(lookup, lookupCount, AnimationIndex(seqs, count, anim), anim,
                                              static_cast<int16_t>(i));
                        seqs[i].id = anim;
                    }
                }

                seqs[i].blendTime &= 0xFFFF;
            }
        }
    }

    bool IsAnimName(std::string_view name)
    {
        return EndsWithCI(name, ".anim");
    }

    bool ProcessInPlaceAndResize(void* bytes, uint32_t& size)
    {
        if (!bytes || size < 8) return false;

        auto* raw = static_cast<uint8_t*>(bytes);
        const uint32_t magic = Rd32(raw);
        if (IsWrappedAnim(magic))
        {
            const uint32_t payloadSize = Rd32(raw + 4);
            if (!payloadSize || payloadSize > size - 8) return false;
            std::memmove(raw, raw + 8, payloadSize);
            size = payloadSize;
            return true;
        }

        auto* md = reinterpret_cast<fmt::M2Header*>(bytes);
        if (md->magic != fmt::kMagicMD20 || !HasSourceVersion(md->version)) return false;

        if (size >= kSequenceHeaderSize)
            FixSequences(md, size);

        md->version = fmt::kClientVersion;
        return true;
    }

    bool ProcessInPlace(void* bytes, uint32_t size)
    {
        uint32_t ignoredSize = size;
        return ProcessInPlaceAndResize(bytes, ignoredSize);
    }

    bool ProcessCopy(std::span<const uint8_t> raw, std::vector<uint8_t>& out)
    {
        if (raw.empty()) return false;
        if (raw.size() >= 8 && IsWrappedAnim(Rd32(raw.data())))
        {
            const uint32_t payloadSize = Rd32(raw.data() + 4);
            if (!payloadSize || payloadSize > raw.size() - 8) return false;
            out.assign(raw.begin() + 8, raw.begin() + 8 + payloadSize);
            return true;
        }

        out.assign(raw.begin(), raw.end());
        uint32_t size = static_cast<uint32_t>(out.size());
        if (!ProcessInPlaceAndResize(out.data(), size))
        {
            out.clear();
            return false;
        }
        out.resize(size);
        return true;
    }
}
