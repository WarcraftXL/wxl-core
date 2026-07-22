// Skin: rebuild the material / texunit contract a source skin omits, at skin finalize.
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

#include "Skin.hpp"

#include "common/Log.hpp"

#include <cstdlib>
#include <cstring>
#include <vector>

namespace wxl::modern::assets::m2::skin
{
    namespace fmt = wxl::structure::m2;
    namespace bn  = wxl::modern::assets::common::bones;
    using Skin     = wxl::game::m2::M2SkinProfile;

    namespace
    {
        // A texunit shaderId >= this is a source shader-effect index (named modern effect), not a packed
        // blend-bit pair; it is decoded by the effect-index branch below.
        constexpr uint16_t kSourceShaderMin = 0x8000;

        /**
         * @brief Appends an entry, returning the index of an existing single-entry match or the new slot.
         * @param lookup Lookup array to search and append into.
         * @param v      Value to find or append.
         * @return Index of the matching or newly appended slot.
         */
        uint16_t LookupSingle(std::vector<int16_t>& lookup, int16_t v)
        {
            for (size_t n = 0; n < lookup.size(); ++n)
                if (lookup[n] == v) return static_cast<uint16_t>(n);
            lookup.push_back(v);
            return static_cast<uint16_t>(lookup.size() - 1);
        }

        /**
         * @brief Appends an adjacent pair, reusing an overlapping tail so [base] and [base+1] equal a and b.
         * @param lookup Lookup array to search and append into.
         * @param a      First value of the pair.
         * @param b      Second value of the pair.
         * @return Base index where the pair sits.
         */
        uint16_t LookupPair(std::vector<int16_t>& lookup, int16_t a, int16_t b)
        {
            for (size_t n = 0; n + 1 < lookup.size(); ++n)
                if (lookup[n] == a && lookup[n + 1] == b) return static_cast<uint16_t>(n);
            size_t sz = lookup.size();
            if (sz > 1 && lookup[sz - 1] == a) { lookup.push_back(b); return static_cast<uint16_t>(sz - 1); }
            lookup.push_back(a);
            lookup.push_back(b);
            return static_cast<uint16_t>(sz);
        }

        /**
         * @brief Finds or appends a (blend1, blend2) pair in textureCombinerCombos.
         * @param combos textureCombinerCombos array to search and append into.
         * @param b1     First blend value.
         * @param b2     Second blend value.
         * @return Base index of the matching or newly appended pair.
         */
        uint16_t BlendOverride(std::vector<uint16_t>& combos, uint16_t b1, uint16_t b2)
        {
            for (size_t n = 0; n + 1 < combos.size(); n += 2)
                if (combos[n] == b1 && combos[n + 1] == b2) return static_cast<uint16_t>(n);
            uint16_t base = static_cast<uint16_t>(combos.size());
            combos.push_back(b1);
            combos.push_back(b2);
            return base;
        }

        uint16_t NextLookup(uint16_t base, uint32_t count)
        {
            const uint32_t next = static_cast<uint32_t>(base) + 1u;
            return next < count ? static_cast<uint16_t>(next) : base;
        }

        /**
         * @brief Parks LOD submeshes and clamps drawn-submesh bone counts to the client ceiling.
         *
         * A level>0 (LOD) submesh has its geometry zeroed with boneCount kept >= 1 (native finalize divides
         * the budget by every submesh boneCount). A drawn submesh's boneCount is clamped to the client ceiling
         * and the boneCombos bounds. A zero-geometry section is marked bad.
         * @param md        Parsed model header.
         * @param skin      Live skin profile whose submeshes are adjusted in place.
         * @param badSubmesh Receives a per-submesh flag, set for zero-geometry sections.
         */
        void FixSubmeshes(fmt::M2Header* md, Skin* skin, std::vector<uint8_t>& badSubmesh,
                          bool extendedIndexStart)
        {
            badSubmesh.assign(skin->submeshCount, 0);
            for (uint32_t i = 0; i < skin->submeshCount; ++i)
            {
                auto* s = &skin->submeshes[i];
                if (s->level > 0 && !extendedIndexStart)
                {
                    s->level = 0; s->vertexStart = 0; s->vertexCount = 0; s->indexStart = 0;
                    s->indexCount = 0; s->boneComboIndex = 0; s->centerBoneIndex = 0;
                }
                else if (s->indexCount != 0)
                {
                    const uint32_t start = bn::FullIndexStart(*s, extendedIndexStart);
                    if (start > skin->indexCount || s->indexCount > skin->indexCount - start)
                    {
                        WLOG_WARN("modern-assets: submesh %u index window past skin indexCount, parking", i);
                        s->level = 0; s->vertexStart = 0; s->vertexCount = 0; s->indexStart = 0;
                        s->indexCount = 0; s->boneComboIndex = 0; s->centerBoneIndex = 0;
                    }
                }

                if (s->indexCount == 0)
                {
                    if (s->boneCount < 1) s->boneCount = 1;
                    badSubmesh[i] = 1;
                }
                else
                {
                    uint16_t cap = bn::kMaxBonesPerDraw;
                    uint16_t byCombo = md->boneCombos.count > s->boneComboIndex
                                     ? static_cast<uint16_t>(md->boneCombos.count - s->boneComboIndex) : 1;
                    if (byCombo < cap) cap = byCombo;
                    if (cap < 1)       cap = 1;
                    if (s->boneCount > cap) s->boneCount = cap;
                    if (s->boneCount < 1)   s->boneCount = 1;
                    if (s->boneInfluences == 0) s->boneInfluences = 1;
                }
                reinterpret_cast<uint8_t*>(s)[0x11] = 0;
            }
        }

        /**
         * @brief Decodes a < kSourceShaderMin shaderId's blend bits and synthesizes its texUnitLookup entries,
         *        in place.
         * @param b             Batch holding the decoded shaderId, updated in place.
         * @param shaderId      Packed blend-bit shaderId to decode.
         * @param textureCount  Source texture count for the batch.
         * @param texUnitLookup textureUnitLookup array to append into.
         * @param blendOverride textureCombinerCombos array to append into.
         */
        void DecodeBlendBits(fmt::M2Batch* b, uint16_t shaderId, uint16_t textureCount,
                             std::vector<int16_t>& texUnitLookup, std::vector<uint16_t>& blendOverride)
        {
            uint16_t blend1 = (shaderId >> 4) & 0x7;
            uint16_t blend2 = shaderId & 0x7;

            bool twoTex = textureCount > 1 && (shaderId & 0x4000) && blend1 != 0 && blend2 != 0;
            uint16_t shaderToSave = 0;
            if (twoTex) shaderToSave = BlendOverride(blendOverride, blend1, blend2);
            else        textureCount = 1;

            b->flags &= 0x10;
            b->shaderId = shaderToSave;

            if (textureCount == 1)
            {
                int16_t t0 = (shaderId & 0x80) ? -1 : 0;
                b->textureCoordComboIndex = LookupSingle(texUnitLookup, t0);
            }
            else
            {
                int16_t t0, t1;
                if (shaderId & 0x80) { t0 = -1; t1 = (shaderId & 0x8) ? -1 : 0; }
                else { t0 = 0; t1 = (shaderId & 0x8) ? -1 : ((shaderId & 0x4000) ? 1 : 0); }
                b->textureCoordComboIndex = LookupPair(texUnitLookup, t0, t1);
            }

            b->textureCount = textureCount < 2 ? textureCount : 2;
        }

        /**
         * @brief Splits one source env batch into a primary plus a follower 2nd render pass over the same
         *        geometry.
         *
         * The follower copies the primary, then carries material-layer 1 and material index +1 so the engine
         * binds it as the layered 2nd pass over the diffuse below it.
         * @param low                 Low byte of the source shaderId selecting the env-split case.
         * @param shaderId            Full source shaderId.
         * @param primary             Batch to split (passed by value).
         * @param nTransparencyLookup Count of transparency-lookup entries.
         * @param out                 Receives the primary and follower batches.
         * @param texUnitLookup       textureUnitLookup array to append into.
         * @param blendOverride       textureCombinerCombos array to append into.
         * @return true if this low code is an env split; false otherwise (caller falls through to the in-place
         *         decode).
         */
        bool EnvSplit(uint16_t low, uint16_t shaderId, fmt::M2Batch primary, uint32_t nTransparencyLookup,
                      uint32_t nTransformLookup,
                      std::vector<fmt::M2Batch>& out, std::vector<int16_t>& texUnitLookup,
                      std::vector<uint16_t>& blendOverride)
        {
            fmt::M2Batch follower = primary;
            follower.materialIndex = static_cast<uint16_t>(primary.materialIndex + 1);
            follower.materialLayer = 1;
            follower.textureCount  = 1;

            switch (low)
            {
            case 0: case 3: case 9: case 17: case 24:
            {
                primary.textureCount = 2;
                uint16_t blendIdx = BlendOverride(blendOverride, 1, 4);
                uint16_t tc = LookupPair(texUnitLookup, 0, -1);
                primary.shaderId = blendIdx; primary.textureCoordComboIndex = tc;
                follower.shaderId = blendIdx; follower.textureCoordComboIndex = tc;
                out.push_back(primary); out.push_back(follower);
                return true;
            }
            case 1: case 15:
            {
                primary.textureCount = 1;
                primary.shaderId = 0; follower.shaderId = 0;
                follower.textureComboIndex = static_cast<uint16_t>(primary.textureComboIndex + 1);
                if (static_cast<uint32_t>(primary.textureWeightComboIndex) + 1 < nTransparencyLookup)
                    follower.textureWeightComboIndex = static_cast<uint16_t>(primary.textureWeightComboIndex + 1);
                follower.textureTransformComboIndex = NextLookup(primary.textureTransformComboIndex, nTransformLookup);
                int16_t t1 = (shaderId == 0x8001) ? -1 : 1;
                uint16_t tc = LookupPair(texUnitLookup, 0, t1);
                primary.textureCoordComboIndex  = tc;
                follower.textureCoordComboIndex = static_cast<uint16_t>(tc + 1);
                out.push_back(primary); out.push_back(follower);
                return true;
            }
            case 2:
            {
                uint16_t blendIdx = BlendOverride(blendOverride, 1, 3);
                uint16_t tc = LookupPair(texUnitLookup, 0, -1);
                primary.shaderId = blendIdx; primary.textureCoordComboIndex = tc;
                follower.shaderId = blendIdx; follower.textureCoordComboIndex = tc;
                out.push_back(primary); out.push_back(follower);
                return true;
            }
            default:
                return false;
            }
        }

        /**
         * @brief Reshapes one batch into 'piece' (1 batch, or primary plus follower for an env split).
         *
         * Does not touch skinSectionIndex; the caller re-points it per target sub-section.
         * @param b                   Source batch (passed by value).
         * @param nTransparencyLookup Count of transparency-lookup entries.
         * @param piece               Receives the reshaped batch(es).
         * @param texUnitLookup       textureUnitLookup array to append into.
         * @param blendOverride       textureCombinerCombos array to append into.
         */
        void DownConvertBatch(fmt::M2Batch b, uint32_t nTransparencyLookup, uint32_t nTransformLookup,
                              std::vector<fmt::M2Batch>& piece,
                              std::vector<int16_t>& texUnitLookup, std::vector<uint16_t>& blendOverride)
        {
            uint16_t shaderId = b.shaderId;
            uint16_t textureCount = b.textureCount;
            b.flags &= 0x10;

            if (shaderId >= kSourceShaderMin)
            {
                // Source shader-effect indices [0..2] are Diffuse_T1_Env effects the engine renders
                // natively, re-based to engine index = sourceIdx+1 (index 0 is "no shader"). Emit ONE
                // 2-texture batch (T1 + env coord); do NOT route these through the EnvSplit heuristic.
                uint16_t sourceIdx = shaderId & 0x7fff;
                if (sourceIdx <= 2)
                {
                    b.shaderId               = static_cast<uint16_t>(kSourceShaderMin | (sourceIdx + 1));
                    b.textureCount           = 2;
                    b.textureCoordComboIndex = LookupPair(texUnitLookup, 0, -1);
                    b.flags                 &= 0x10;
                    piece.push_back(b);
                    return;
                }

                uint16_t low = shaderId & 0xFF;
                if (EnvSplit(low, shaderId, b, nTransparencyLookup, nTransformLookup,
                             piece, texUnitLookup, blendOverride))
                    return;
                switch (low)
                {
                case 5: case 8: case 10: case 12: case 16: case 23:
                    shaderId = 0; textureCount = 1; break;
                case 21:
                    shaderId = 0x4011; textureCount = 2; break;
                default:
                    shaderId = 0x0010; textureCount = 1; break;
                }
            }

            if (shaderId < kSourceShaderMin)
                DecodeBlendBits(&b, shaderId, textureCount, texUnitLookup, blendOverride);
            else
                b.textureCount = textureCount < 2 ? textureCount : 2;

            piece.push_back(b);
        }

        /**
         * @brief Builds the reshaped batch array.
         *
         * Each original batch is processed once, then emitted for every sub-section its original submesh
         * became (skinSectionIndex re-pointed). A batch on a bad section is reduced to a no-draw batch. With no
         * bone split, splitMap is empty and every batch maps 1:1 (run = {origSection, 1}).
         * @param skin                Live skin profile providing the source batches.
         * @param badSubmesh          Per-submesh bad flags from FixSubmeshes.
         * @param splitMap            Per-original-submesh sub-section runs (empty when no split occurred).
         * @param out                 Receives the reshaped batch array.
         * @param texUnitLookup       textureUnitLookup array to append into.
         * @param blendOverride       textureCombinerCombos array to append into.
         * @param nTransparencyLookup Count of transparency-lookup entries.
         */
        void FixTexUnits(Skin* skin, const std::vector<uint8_t>& badSubmesh,
                         const std::vector<bn::SplitRun>& splitMap, std::vector<fmt::M2Batch>& out,
                         std::vector<int16_t>& texUnitLookup, std::vector<uint16_t>& blendOverride,
                         uint32_t nTransparencyLookup, uint32_t nTransformLookup)
        {
            out.reserve(skin->batchCount);
            for (uint32_t i = 0; i < skin->batchCount; ++i)
            {
                fmt::M2Batch b = skin->batches[i];

                bn::SplitRun run{ b.skinSectionIndex, 1 };
                if (b.skinSectionIndex < splitMap.size()) run = splitMap[b.skinSectionIndex];

                std::vector<fmt::M2Batch> piece;
                DownConvertBatch(b, nTransparencyLookup, nTransformLookup, piece, texUnitLookup, blendOverride);

                for (uint16_t s = 0; s < run.count; ++s)
                {
                    uint16_t sectionIdx = static_cast<uint16_t>(run.first + s);
                    bool bad = sectionIdx < badSubmesh.size() && badSubmesh[sectionIdx];
                    for (const fmt::M2Batch& p : piece)
                    {
                        fmt::M2Batch nb = p;
                        nb.skinSectionIndex = sectionIdx;
                        if (bad) nb.shaderId = 0x8000;
                        out.push_back(nb);
                    }
                }
            }
        }

        void EnsureTextureTransformLookup(fmt::M2Header* md, const Skin* skin, const char* name)
        {
            if (!md || !skin) return;
            if (!md->textureTransforms.count || md->textureTransformCombos.count) return;

            uint32_t needed = md->textureTransforms.count;
            for (uint32_t i = 0; i < skin->batchCount; ++i)
            {
                const auto& b = skin->batches[i];
                const uint32_t base = b.textureTransformComboIndex;
                if (base == 0xFFFFu) continue;
                const uint32_t count = b.textureCount ? b.textureCount : 1u;
                if (base <= 0xFFFFu - count && needed < base + count)
                    needed = base + count;
            }

            auto* buf = static_cast<int16_t*>(std::malloc(needed * sizeof(int16_t)));
            if (!buf) return;
            for (uint32_t i = 0; i < needed; ++i)
                buf[i] = i < md->textureTransforms.count ? static_cast<int16_t>(i) : static_cast<int16_t>(-1);

            md->textureTransformCombos.count  = needed;
            md->textureTransformCombos.offset = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(buf));
            WLOG_INFO("modern-assets: '%s' synthesized %u texture-transform lookup entries", name, needed);
        }

        /// Material render-flag bits this client understands and that must survive the mask.
        ///   0x00-0x1F : the classic 3.3.5 render flags (unlit, no-fog, two-sided, ...).
        ///   0x40      : SHADOW-EXCLUDE. The stock caster collector (0x00834660) tests it and drops the
        ///               batch outright -- this is how a modern tree's canopy or a glow plane is kept out
        ///               of the shadow pass, exactly as Legion does via its skin shadow_batches list.
        ///   0x80      : SHADOW-FORCE. Same collector: casts even when the blend mode would not qualify.
        /// Masking 0x40/0x80 away (this used to be `& 0x1F`) silently re-admitted geometry the file
        /// explicitly opts out of, and destroyed the only shadow-exclusion channel 3.3.5 natively reads.
        constexpr uint16_t kMaterialFlagMask = 0x1Fu | 0x40u | 0x80u;

        /**
         * @brief Clamps source material blend modes the client blend table cannot index to Add, and drops
         *        only the render-flag bits this client has no meaning for.
         *
         * A blend mode above 6 is set to Add (4). materials.offset is a raw pointer here.
         * @param md Parsed model header whose material array is adjusted in place.
         */
        void FixRenderFlags(fmt::M2Header* md)
        {
            if (!md->materials.count || !md->materials.offset) return;
            auto* mats = reinterpret_cast<uint16_t*>(static_cast<uintptr_t>(md->materials.offset));
            for (uint32_t i = 0; i < md->materials.count; ++i)
            {
                uint16_t& flag  = mats[i * 2 + 0];
                uint16_t& blend = mats[i * 2 + 1];
                if (blend > 6) { blend = 4; flag |= 0x5; }
                flag &= kMaterialFlagMask;
            }
        }
    }

    /**
     * @brief Rebuilds the material / texunit contract on a downported model's live skin profile.
     * @param md        Parsed model header (arrays are raw pointers by this point).
     * @param skin      Attached live skin profile.
     * @param splitMap  Per-original-submesh sub-section run already applied to skin by the caller's bone
     *                  split; empty when no split occurred.
     * @param name      Model path, used for logging.
     */
    void Rebuild(fmt::M2Header* md, Skin* skin, const std::vector<bn::SplitRun>& splitMap, const char* name)
    {
        if (!md || !skin) return;

        if (skin->batchCount > bn::kMaxBatches)
        {
            WLOG_WARN("modern-m2: '%s' skin batchCount=%u exceeds cap, clamping", name, skin->batchCount);
            skin->batchCount = bn::kMaxBatches;
        }

        std::vector<uint8_t>      badSubmesh;
        std::vector<int16_t>      texUnitLookup;
        std::vector<uint16_t>     blendOverride;
        std::vector<fmt::M2Batch> batches;
        uint32_t nTransparencyLookup = md->textureWeightCombos.count;
        EnsureTextureTransformLookup(md, skin, name);
        uint32_t nTransformLookup = md->textureTransformCombos.count;

        // The bone split itself already ran unconditionally (ModernM2::OnSkinFinalize) before this is
        // called; splitMap is that result. FixSubmeshes/FixTexUnits below operate on the already-split
        // skin->submeshes and combine the split re-point with this source's shaderId decode.
        const bool extendedIndexStart = bn::UsesExtendedIndexStart(name);
        FixSubmeshes(md, skin, badSubmesh, extendedIndexStart);
        FixTexUnits(skin, badSubmesh, splitMap, batches, texUnitLookup, blendOverride,
                    nTransparencyLookup, nTransformLookup);

        // Commit the rebuilt batch array BEFORE native finalize sizes its parallel block from
        // skin->batchCount. The file-mapped arrays are never per-array freed, so the new buffer is leaked
        // for the model's lifetime (same pattern as the header arrays below).
        if (!batches.empty())
        {
            auto* buf = static_cast<fmt::M2Batch*>(std::malloc(batches.size() * sizeof(fmt::M2Batch)));
            if (buf)
            {
                std::memcpy(buf, batches.data(), batches.size() * sizeof(fmt::M2Batch));
                skin->batches    = buf;
                skin->batchCount = static_cast<uint32_t>(batches.size());
            }
        }

        if (!texUnitLookup.empty())
        {
            auto* buf = static_cast<int16_t*>(std::malloc(texUnitLookup.size() * sizeof(int16_t)));
            if (buf)
            {
                std::memcpy(buf, texUnitLookup.data(), texUnitLookup.size() * sizeof(int16_t));
                md->textureUnitLookup.count  = static_cast<uint32_t>(texUnitLookup.size());
                md->textureUnitLookup.offset = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(buf));
            }
        }

        if (!blendOverride.empty())
        {
            auto* buf = static_cast<uint16_t*>(std::malloc(blendOverride.size() * sizeof(uint16_t)));
            if (buf)
            {
                std::memcpy(buf, blendOverride.data(), blendOverride.size() * sizeof(uint16_t));
                md->textureCombinerCombos.count  = static_cast<uint32_t>(blendOverride.size());
                md->textureCombinerCombos.offset = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(buf));
                md->globalFlags |= fmt::kFlagUseTextureCombinerCombos;
            }
        }

        FixRenderFlags(md);
    }
}
