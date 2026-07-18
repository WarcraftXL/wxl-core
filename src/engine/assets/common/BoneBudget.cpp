// Bone budget: partition a submesh whose per-draw bone palette exceeds the client ceiling into
// sub-sections, and re-point batches across the resulting sub-section run.
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

#include "BoneBudget.hpp"

#include "common/Log.hpp"

#include "engine/assets/shared/common/Text.hpp"

#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace wxl::modern::assets::common::bones
{
    namespace fmt  = wxl::structure::m2;
    namespace text = wxl::modern::assets::common::text;
    using Skin     = wxl::game::m2::M2SkinProfile;

    namespace
    {
        constexpr uint32_t kSkinU16Max         = 0xFFFF;  // u16 ceiling of the skin arrays
        constexpr uint32_t kSplitMaxBoneCombos = 0x10000; // boneCombos upper bound, rejected before bulk copy
    }

    bool UsesExtendedIndexStart(std::string_view name)
    {
        return text::StartsWithCI(name, "character\\") ||
               text::StartsWithCI(name, "item\\objectcomponents\\");
    }

    uint32_t FullIndexStart(const fmt::M2SkinSection& section, bool extended)
    {
        return extended ? ((static_cast<uint32_t>(section.level) << 16) | section.indexStart)
                        : static_cast<uint32_t>(section.indexStart);
    }

    /**
     * @brief Partitions over-ceiling submeshes into sub-sections, rebuilding the live skin geometry and
     *        header.boneCombos.
     * @param md          Parsed model header (boneCombos array is a raw pointer here).
     * @param skin        Live skin profile whose geometry arrays are rebuilt on success.
     * @param outSections Receives the rebuilt sub-sections.
     * @param splitMap    Receives the per-original-submesh sub-section run, indexed by original submesh.
     * @param splitCount  Receives the count of extra sub-draws produced.
     * @param name        Model path, used for logging.
     * @return true on commit; false (no commit) on any overflow, allocation failure, or missing array.
     */
    bool SplitSubmeshes(fmt::M2Header* md, Skin* skin, std::vector<SplitSection>& outSections, std::vector<SplitRun>& splitMap, uint32_t& splitCount, const char* name)
    {
        if (!md->boneCombos.count || !md->boneCombos.offset) return false;
        if (!skin->vertexLookup || !skin->indices || !skin->bones) return false;

        uint32_t boneComboCount = md->boneCombos.count;
        auto* boneCombos = reinterpret_cast<uint16_t*>(static_cast<uintptr_t>(md->boneCombos.offset));
        if (boneComboCount > kSplitMaxBoneCombos || !boneCombos)
        {
            WLOG_WARN("modern-assets: '%s' boneCombos count=%u out of range, skipping bone split", name, boneComboCount);
            return false;
        }

        bool needsSplit = false;
        const bool extendedIndexStart = UsesExtendedIndexStart(name);
        for (uint32_t si = 0; si < skin->submeshCount; ++si)
        {
            const fmt::M2SkinSection& s = skin->submeshes[si];
            if ((extendedIndexStart || s.level == 0) && s.boneCount > kMaxBonesPerDraw)
                { needsSplit = true; break; }
        }
        if (!needsSplit) return false;

        std::vector<uint16_t> newVtxLookup;
        std::vector<uint8_t>  newBones;
        std::vector<uint16_t> newIndices;
        std::vector<uint16_t> newBoneCombos(boneCombos, boneCombos + boneComboCount);
        newVtxLookup.reserve(skin->vertexCount);
        newBones.reserve(skin->vertexCount * 4);
        newIndices.reserve(skin->indexCount);

        splitCount = 0;

        for (uint32_t si = 0; si < skin->submeshCount; ++si)
        {
            fmt::M2SkinSection s = skin->submeshes[si];

            // A level>0 submesh is a sub-batch the engine cannot draw. Pass it through as a single zeroed
            // placeholder so the batch re-point stays 1:1; its batch is later skipped.
            if (s.level > 0 && !extendedIndexStart)
            {
                s.level = 0; s.vertexStart = 0; s.vertexCount = 0; s.indexStart = 0;
                s.indexCount = 0; s.boneComboIndex = 0; s.centerBoneIndex = 0; s.boneCount = 1;
                reinterpret_cast<uint8_t*>(&s)[0x11] = 0;
                outSections.push_back({ s, static_cast<uint16_t>(si) });
                continue;
            }

            const uint32_t sourceIndexStart = FullIndexStart(s, extendedIndexStart);
            if (sourceIndexStart > skin->indexCount || s.indexCount > skin->indexCount - sourceIndexStart)
            {
                WLOG_WARN("modern-assets: '%s' submesh %u index window past skin indexCount, skipping bone split", name, si);
                return false;
            }

            uint32_t triCount = s.indexCount / 3;
            uint32_t comboBase = s.boneComboIndex;

            std::vector<uint16_t> curGlobals;
            uint32_t curTriStart = 0;
            uint32_t emittedSections = 0;

            // Emit triangles [triFrom, triTo) as one sub-section: a sorted boneCombos slice, a deduplicated
            // vertex block with bones[] remapped to the slice, and a global-indexed triangle block.
            auto emit = [&](uint32_t triFrom, uint32_t triTo, std::vector<uint16_t>& globals) -> bool
            {
                if (triFrom >= triTo) return true;
                std::sort(globals.begin(), globals.end());
                uint32_t comboIndex = static_cast<uint32_t>(newBoneCombos.size());
                if (comboIndex > kSkinU16Max) return false;
                for (uint16_t g : globals) newBoneCombos.push_back(g);

                uint32_t secVertStart  = static_cast<uint32_t>(newVtxLookup.size());
                uint32_t secIndexStart = static_cast<uint32_t>(newIndices.size());
                if (secVertStart > kSkinU16Max) return false;
                if (!extendedIndexStart && secIndexStart > kSkinU16Max) return false;
                if (extendedIndexStart && (secIndexStart >> 16) > kSkinU16Max) return false;

                std::unordered_map<uint16_t, uint16_t> vmap;
                for (uint32_t t = triFrom; t < triTo; ++t)
                {
                    for (uint32_t k = 0; k < 3; ++k)
                    {
                        uint16_t lv = skin->indices[sourceIndexStart + t * 3 + k];
                        if (lv >= skin->vertexCount) return false;
                        auto it = vmap.find(lv);
                        uint16_t nv;
                        if (it == vmap.end())
                        {
                            uint32_t idx = static_cast<uint32_t>(newVtxLookup.size());
                            if (idx > kSkinU16Max) return false;
                            nv = static_cast<uint16_t>(idx);
                            vmap.emplace(lv, nv);
                            newVtxLookup.push_back(skin->vertexLookup[lv]);
                            const uint8_t* infl = skin->bones + lv * 4;
                            for (uint32_t j = 0; j < 4; ++j)
                            {
                                uint32_t comboIdx = comboBase + infl[j];
                                uint16_t g = comboIdx < boneComboCount ? boneCombos[comboIdx] : globals[0];
                                auto lo = std::lower_bound(globals.begin(), globals.end(), g);
                                uint16_t local = (lo != globals.end() && *lo == g)
                                               ? static_cast<uint16_t>(lo - globals.begin()) : 0;
                                newBones.push_back(static_cast<uint8_t>(local));
                            }
                        }
                        else nv = it->second;
                        newIndices.push_back(nv);
                    }
                }

                uint32_t secVertCount  = static_cast<uint32_t>(newVtxLookup.size()) - secVertStart;
                uint32_t secIndexCount = static_cast<uint32_t>(newIndices.size()) - secIndexStart;
                if (secVertCount > kSkinU16Max || secIndexCount > kSkinU16Max) return false;

                fmt::M2SkinSection sec = s;
                sec.vertexStart    = static_cast<uint16_t>(secVertStart);
                sec.vertexCount    = static_cast<uint16_t>(secVertCount);
                sec.level          = extendedIndexStart ? static_cast<uint16_t>(secIndexStart >> 16) : 0;
                sec.indexStart     = static_cast<uint16_t>(secIndexStart & kSkinU16Max);
                sec.indexCount     = static_cast<uint16_t>(secIndexCount);
                sec.boneCount      = static_cast<uint16_t>(globals.size());
                sec.boneComboIndex = static_cast<uint16_t>(comboIndex);
                outSections.push_back({ sec, static_cast<uint16_t>(si) });
                ++emittedSections;
                return true;
            };

            for (uint32_t t = 0; t < triCount; ++t)
            {
                uint16_t g[12]; int gn = 0;
                for (uint32_t k = 0; k < 3; ++k)
                {
                    uint16_t lv = skin->indices[sourceIndexStart + t * 3 + k];
                    if (lv >= skin->vertexCount) return false;
                    const uint8_t* infl = skin->bones + lv * 4;
                    for (uint32_t j = 0; j < 4; ++j)
                    {
                        uint32_t comboIdx = comboBase + infl[j];
                        uint16_t gg = comboIdx < boneComboCount ? boneCombos[comboIdx] : 0;
                        bool seen = false;
                        for (int e = 0; e < gn; ++e) if (g[e] == gg) { seen = true; break; }
                        if (!seen && gn < 12) g[gn++] = gg;
                    }
                }
                size_t unionSize = curGlobals.size();
                for (int e = 0; e < gn; ++e)
                    if (std::find(curGlobals.begin(), curGlobals.end(), g[e]) == curGlobals.end())
                        ++unionSize;

                if (unionSize > kMaxBonesPerDraw && t > curTriStart)
                {
                    if (!emit(curTriStart, t, curGlobals)) return false;
                    curGlobals.clear();
                    curTriStart = t;
                }
                for (int e = 0; e < gn; ++e)
                    if (std::find(curGlobals.begin(), curGlobals.end(), g[e]) == curGlobals.end())
                        curGlobals.push_back(g[e]);
            }
            if (!emit(curTriStart, triCount, curGlobals)) return false;
            if (emittedSections == 0)
            {
                fmt::M2SkinSection sec = s;
                sec.vertexCount = 0; sec.indexCount = 0; sec.boneCount = 1;
                outSections.push_back({ sec, static_cast<uint16_t>(si) });
            }
            else if (emittedSections > 1)
            {
                splitCount += emittedSections - 1;
            }
        }

        if (newVtxLookup.size() > kSkinU16Max || outSections.size() > kMaxBatches) return false;

        splitMap.assign(skin->submeshCount, SplitRun{ 0, 0 });
        for (uint16_t i = 0; i < outSections.size(); ++i)
        {
            uint16_t orig = outSections[i].origSubmesh;
            if (orig >= splitMap.size()) continue;
            if (splitMap[orig].count == 0) splitMap[orig].first = i;
            ++splitMap[orig].count;
        }

        // Commit the rebuilt geometry into owned buffers (leaked for the model's lifetime; the engine never
        // per-array frees the file-mapped skin arrays).
        auto* vl = static_cast<uint16_t*>(std::malloc(newVtxLookup.size() * sizeof(uint16_t)));
        auto* bn = static_cast<uint8_t*>(std::malloc(newBones.size()));
        auto* ix = static_cast<uint16_t*>(std::malloc(newIndices.size() * sizeof(uint16_t)));
        auto* bc = static_cast<uint16_t*>(std::malloc(newBoneCombos.size() * sizeof(uint16_t)));
        auto* sm = static_cast<fmt::M2SkinSection*>(std::malloc(outSections.size() * sizeof(fmt::M2SkinSection)));
        if (!vl || !bn || !ix || !bc || !sm)
        {
            std::free(vl); std::free(bn); std::free(ix); std::free(bc); std::free(sm);
            return false;
        }
        std::memcpy(vl, newVtxLookup.data(), newVtxLookup.size() * sizeof(uint16_t));
        std::memcpy(bn, newBones.data(), newBones.size());
        std::memcpy(ix, newIndices.data(), newIndices.size() * sizeof(uint16_t));
        std::memcpy(bc, newBoneCombos.data(), newBoneCombos.size() * sizeof(uint16_t));
        for (size_t i = 0; i < outSections.size(); ++i) sm[i] = outSections[i].section;

        skin->vertexLookup = vl;
        skin->vertexCount  = static_cast<uint32_t>(newVtxLookup.size());
        skin->bones        = bn;
        skin->boneCount    = static_cast<uint32_t>(newVtxLookup.size());
        skin->indices      = ix;
        skin->indexCount   = static_cast<uint32_t>(newIndices.size());
        skin->submeshes    = sm;
        skin->submeshCount = static_cast<uint32_t>(outSections.size());

        md->boneCombos.count  = static_cast<uint32_t>(newBoneCombos.size());
        md->boneCombos.offset = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(bc));
        return true;
    }

    /**
     * @brief Re-points a skin's existing batches across the sub-section run their original submesh
     *        became, duplicating a batch per extra sub-section without touching any other field.
     * @param skin      Live skin profile whose batches are rebuilt in place.
     * @param splitMap  Per-original-submesh sub-section run, as produced by SplitSubmeshes.
     */
    void RepointBatchesAfterSplit(Skin* skin, const std::vector<SplitRun>& splitMap)
    {
        if (splitMap.empty() || !skin->batches || skin->batchCount == 0) return;

        std::vector<fmt::M2Batch> out;
        out.reserve(skin->batchCount);
        for (uint32_t i = 0; i < skin->batchCount; ++i)
        {
            const fmt::M2Batch& b = skin->batches[i];
            SplitRun run{ b.skinSectionIndex, 1 };
            if (b.skinSectionIndex < splitMap.size()) run = splitMap[b.skinSectionIndex];
            for (uint16_t s = 0; s < run.count; ++s)
            {
                fmt::M2Batch nb = b;
                nb.skinSectionIndex = static_cast<uint16_t>(run.first + s);
                out.push_back(nb);
            }
        }
        if (out.empty() || out.size() > kMaxBatches) return;

        // Leaked for the model's lifetime, same pattern as SplitSubmeshes' committed arrays above.
        auto* buf = static_cast<fmt::M2Batch*>(std::malloc(out.size() * sizeof(fmt::M2Batch)));
        if (!buf) return;
        std::memcpy(buf, out.data(), out.size() * sizeof(fmt::M2Batch));
        skin->batches    = buf;
        skin->batchCount = static_cast<uint32_t>(out.size());
    }
}
