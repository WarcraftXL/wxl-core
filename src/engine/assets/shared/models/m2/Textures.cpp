// Synthesizes the texture-coordinate-combos array a source model omits.
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

#include "Textures.hpp"

#include "Contract.hpp"

#include "../../common/Text.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

namespace wxl::modern::assets::m2::textures
{
    namespace fmt  = wxl::structure::m2;
    namespace text = wxl::modern::assets::common::text;

    namespace
    {
        constexpr uint32_t kTextureTransformStride = 0x3C;
        constexpr uint32_t kTrackStride = 0x14;
        constexpr uint32_t kTrackGlobalSeq = 0x02;
        constexpr uint32_t kTrackTimestampsCount = 0x04;
        constexpr uint32_t kTrackTimestampsOfs = 0x08;
        constexpr uint16_t kNoGlobalSequence = 0xFFFFu;
        constexpr uint32_t kMinLoopMs = 100u;
        constexpr uint32_t kMaxLoopMs = 600000u;

        uint32_t Align4(uint32_t value)
        {
            return (value + 3u) & ~3u;
        }

        bool Fits(uint32_t offset, uint32_t count, uint32_t stride, uint32_t size)
        {
            if (!count) return true;
            const uint64_t bytes = uint64_t(count) * uint64_t(stride);
            return uint64_t(offset) + bytes <= size && uint64_t(offset) + bytes >= offset;
        }

        void ExistingLoops(const fmt::M2Header* md, uint32_t fileSize, std::vector<uint32_t>& out)
        {
            out.clear();
            if (!md->globalLoops.count || !md->globalLoops.offset) return;
            if (!Fits(md->globalLoops.offset, md->globalLoops.count, 4, fileSize)) return;

            const uint8_t* loops = md->base() + md->globalLoops.offset;
            out.reserve(md->globalLoops.count);
            for (uint32_t i = 0; i < md->globalLoops.count; ++i)
                out.push_back(Rd32(loops + i * 4));
        }

        uint32_t TrackDurationMs(const fmt::M2Header* md, uint32_t fileSize, const uint8_t* track)
        {
            const uint32_t outerCount = Rd32(track + kTrackTimestampsCount);
            const uint32_t outerOfs = Rd32(track + kTrackTimestampsOfs);
            if (!outerCount || outerCount > 0x1000u || !outerOfs) return 0;
            if (!Fits(outerOfs, outerCount, sizeof(fmt::M2Array), fileSize)) return 0;

            const auto* outer = reinterpret_cast<const fmt::M2Array*>(md->base() + outerOfs);
            uint32_t best = 0;
            for (uint32_t i = 0; i < outerCount; ++i)
            {
                const uint32_t count = outer[i].count;
                const uint32_t offset = outer[i].offset;
                if (count < 2 || count > 0x10000u || !offset) continue;
                if (!Fits(offset, count, 4, fileSize)) continue;

                const uint32_t last = Rd32(md->base() + offset + (count - 1) * 4);
                if (last > best) best = last;
            }
            return best;
        }

        uint16_t FindOrAddLoop(std::vector<uint32_t>& loops, uint32_t duration)
        {
            for (size_t i = 0; i < loops.size(); ++i)
                if (loops[i] == duration) return static_cast<uint16_t>(i);

            if (loops.size() >= 0xFFFEu) return kNoGlobalSequence;
            loops.push_back(duration);
            return static_cast<uint16_t>(loops.size() - 1);
        }

        bool UsableLoopDuration(uint32_t duration)
        {
            return duration >= kMinLoopMs && duration <= kMaxLoopMs;
        }

        bool HasUsableGlobalLoop(uint16_t globalSeq, const std::vector<uint32_t>& loops)
        {
            return globalSeq != kNoGlobalSequence &&
                   globalSeq < loops.size() &&
                   UsableLoopDuration(loops[globalSeq]);
        }

        bool DesiredTrackLoop(const fmt::M2Header* md, uint32_t fileSize, const uint8_t* track,
                              std::vector<uint32_t>& loops, uint16_t& loop,
                              bool isolateEquipmentLoop)
        {
            const uint32_t duration = TrackDurationMs(md, fileSize, track);
            if (!UsableLoopDuration(duration)) return false;

            const uint16_t globalSeq = Rd16(track + kTrackGlobalSeq);
            // Retail equipment commonly assigns continuously scrolling UV effects to either of the first
            // two global slots. On attached models the client's controller can leave those slots at their
            // initial/final key: slot 0 effects then appear static from load, while the observed 10-second
            // slot 1 effect advances once and freezes. A private duplicate above slot 1 gets the normal
            // modulo clock. Repoint only the texture track, leaving bone/particle users untouched.
            const bool isolateEarlySlot = globalSeq <= 1;
            if (isolateEquipmentLoop && isolateEarlySlot)
            {
                for (size_t i = 2; i < loops.size(); ++i)
                {
                    if (loops[i] != duration) continue;
                    loop = static_cast<uint16_t>(i);
                    return true;
                }
                if (loops.size() >= 0xFFFEu) return false;
                // Models with fewer than two loops need harmless pads so the private clock always
                // lands at index 2 or above, including a source with no global-loop table at all.
                while (loops.size() < 2)
                    loops.push_back(duration);
                if (loops.size() >= 0xFFFEu) return false;
                loops.push_back(duration);
                loop = static_cast<uint16_t>(loops.size() - 1);
                return true;
            }

            if (HasUsableGlobalLoop(globalSeq, loops) && loops[globalSeq] == duration)
                return false;

            loop = FindOrAddLoop(loops, duration);
            return loop != kNoGlobalSequence && loop != globalSeq;
        }

        uint32_t BuildRepairedLoops(const fmt::M2Header* md, uint32_t fileSize,
                                    std::vector<uint32_t>& loops, bool isolateEquipmentLoop)
        {
            ExistingLoops(md, fileSize, loops);

            if (!md->textureTransforms.count || !md->textureTransforms.offset) return 0;
            if (!Fits(md->textureTransforms.offset, md->textureTransforms.count, kTextureTransformStride, fileSize))
                return 0;

            uint32_t repaired = 0;
            const size_t originalLoopCount = loops.size();
            const uint8_t* transforms = md->base() + md->textureTransforms.offset;
            for (uint32_t i = 0; i < md->textureTransforms.count; ++i)
            {
                const uint8_t* xf = transforms + i * kTextureTransformStride;
                for (uint32_t t = 0; t < 3; ++t)
                {
                    const uint8_t* track = xf + t * kTrackStride;
                    uint16_t loop = kNoGlobalSequence;
                    if (DesiredTrackLoop(md, fileSize, track, loops, loop, isolateEquipmentLoop))
                        ++repaired;
                }
            }

            if (loops.size() == originalLoopCount)
                return repaired;
            return repaired;
        }
    }

    /**
     * @brief Reports whether the model omits the texture-coordinate-combos array.
     * @param md  Model header.
     * @return True if a one-entry synth is needed.
     */
    bool NeedsCoordCombos(const fmt::M2Header* md)
    {
        return md->textureUnitLookup.count == 0;
    }

    /**
     * @brief Writes a one-entry texture-coordinate-combos array and points header.textureUnitLookup at it.
     * @param md            Model header. The caller must have reserved 2 bytes at appendOffset.
     * @param appendOffset  Model-relative offset where the entry is written.
     */
    void SynthCoordCombos(fmt::M2Header* md, uint32_t appendOffset)
    {
        Wr16(md->base() + appendOffset, 0);    // one entry = texture-coordinate set 0 (standard, no env)
        md->textureUnitLookup.count  = 1;      // header+0x88
        md->textureUnitLookup.offset = appendOffset; // header+0x8C -> the appended entry
    }

    uint32_t TextureTransformLoopRepairExtraBytes(const fmt::M2Header* md, uint32_t fileSize,
                                                  uint32_t appendOffset, std::string_view name)
    {
        if (!md || md->magic != fmt::kMagicMD20) return 0;

        std::vector<uint32_t> loops;
        const bool isolateEquipmentLoop = text::ContainsCI(name, "item\\objectcomponents\\");
        const uint32_t repaired = BuildRepairedLoops(md, fileSize, loops, isolateEquipmentLoop);
        if (!repaired || loops.size() == md->globalLoops.count) return 0;

        const uint32_t aligned = Align4(appendOffset);
        const uint64_t bytes = uint64_t(aligned - appendOffset) + uint64_t(loops.size()) * 4u;
        return bytes > 0xFFFFFFFFull ? 0 : static_cast<uint32_t>(bytes);
    }

    uint32_t RepairTextureTransformLoops(fmt::M2Header* md, uint32_t fileSize,
                                         uint32_t appendOffset, uint32_t workSize,
                                         std::string_view name)
    {
        if (!md || md->magic != fmt::kMagicMD20) return 0;

        std::vector<uint32_t> loops;
        const bool isolateEquipmentLoop = text::ContainsCI(name, "item\\objectcomponents\\");
        const uint32_t repaired = BuildRepairedLoops(md, fileSize, loops, isolateEquipmentLoop);
        if (!repaired) return 0;

        if (loops.size() != md->globalLoops.count)
        {
            appendOffset = Align4(appendOffset);
            const uint64_t bytes = uint64_t(loops.size()) * 4u;
            if (uint64_t(appendOffset) + bytes > workSize) return 0;

            uint8_t* out = md->base() + appendOffset;
            for (uint32_t i = 0; i < loops.size(); ++i)
                Wr32(out + i * 4, loops[i]);
            md->globalLoops.count = static_cast<uint32_t>(loops.size());
            md->globalLoops.offset = appendOffset;
        }

        uint32_t written = 0;
        uint8_t* transforms = md->base() + md->textureTransforms.offset;
        for (uint32_t i = 0; i < md->textureTransforms.count; ++i)
        {
                uint8_t* xf = transforms + i * kTextureTransformStride;
                for (uint32_t t = 0; t < 3; ++t)
                {
                    uint8_t* track = xf + t * kTrackStride;
                    uint16_t loop = kNoGlobalSequence;
                    if (!DesiredTrackLoop(md, fileSize, track, loops, loop, isolateEquipmentLoop)) continue;
                    Wr16(track + kTrackGlobalSeq, loop);
                    ++written;
                }
        }
        return written;
    }

    WeaponBladeFixResult FixWeaponBladeTextureTypes(fmt::M2Header* md, uint32_t fileSize)
    {
        WeaponBladeFixResult result;
        if (!md || md->magic != fmt::kMagicMD20) return result;
        if (md->textures.offset > fileSize) return result;
        if (md->textures.count > (fileSize - md->textures.offset) / sizeof(fmt::M2Texture)) return result;

        auto* textures = reinterpret_cast<fmt::M2Texture*>(md->base() + md->textures.offset);
        for (uint32_t i = 0; i < md->textures.count; ++i)
        {
            if (textures[i].type != fmt::kTexTypeWeaponBlade) continue;

            const auto& filename = textures[i].filename;
            const bool hasFilename = filename.count != 0;
            if (hasFilename && (!filename.offset || !Fits(filename.offset, filename.count, 1, fileSize)))
                continue;
            textures[i].type = hasFilename ? fmt::kTexTypeHardcoded : fmt::kTexTypeObjectSkin;
            if (hasFilename)
                ++result.toHardcoded;
            else
                ++result.toObjectSkin;
        }
        return result;
    }

    bool AllowsWeaponBladeRemap(std::string_view name)
    {
        if (text::StartsWithCI(name, "creature\\") || text::StartsWithCI(name, "character\\")) return true;
        if (!text::StartsWithCI(name, "item\\objectcomponents\\")) return false;
        return !text::StartsWithCI(name, "item\\objectcomponents\\weapon\\");
    }
}
