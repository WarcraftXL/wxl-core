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

#include "Loader.hpp"
#include "M2Runtime.hpp"
#include "BonesSplitter.hpp"

#include "Hook.hpp"
#include "Mem.hpp"
#include "Logger.hpp"
#include "M2.hpp"
#include "Texture.hpp"

#include <windows.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace sdk  = wraith::structure::m2;
namespace off  = wraith::offsets::game::m2;
namespace tex  = wraith::offsets::game::texture;
namespace mem  = wraith::core::mem;
namespace bs   = wraith::runtime::bonessplitter;

// Runtime portion of the modern model loader. The host has already produced a target-shaped model
// (de-chunked, record strides compacted), but two things can only be done against the LIVE engine
// objects: (1) the native version gate still rejects the Source inner versions, and (2) the material /
// skin contract a Source skin omits must be rebuilt against the engine's parsed, pointer-fixed skin
// object. A Source skin encodes each batch's material in its own shaderId and leaves the header
// textureUnitLookup empty; the native first shader-id pass indexes textureUnitLookup[batch.texCoordCombo]
// and would NULL-deref. The rebuild runs at the skin-finalize entry, where the header and the
// pointer-fixed skin are live and BEFORE the shader passes size their parallel batch blocks.
namespace wraith::runtime::m2
{
    namespace
    {
        off::M2_InitFn         g_initOriginal     = nullptr;
        off::M2_FinalizeSkinFn g_finalizeOriginal = nullptr;
        off::M2_RibbonDeRelocateFn g_ribbonDeRelocOriginal = nullptr;

        bool IsSourceModel(const sdk::M2Header* md)
        {
            return md && md->magic == sdk::kMagicMD20 &&
                   md->version >= sdk::kSourceVersionMin && md->version <= sdk::kSourceVersionMax;
        }

        // ---- material contract rebuild ----------------------------------------------------------------

        // A skin batch/submesh count past this is treated as malformed; the env split can up to double
        // the batch count, so cap the commit well under any value that would overflow the native sizing.
        constexpr uint32_t kMaxBatches = 0x4000;

        // Append entry, returning the index of an existing single-entry match or the new slot.
        uint16_t LookupSingle(std::vector<int16_t>& lookup, int16_t v)
        {
            for (size_t n = 0; n < lookup.size(); ++n)
                if (lookup[n] == v) return static_cast<uint16_t>(n);
            lookup.push_back(v);
            return static_cast<uint16_t>(lookup.size() - 1);
        }

        // Append an adjacent pair, returning the base index. Reuses an overlapping tail so [base],[base+1]
        // equal a, b.
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

        // Find/append a (blend1, blend2) pair in textureCombinerCombos, returning its base index.
        uint16_t BlendOverride(std::vector<uint16_t>& combos, uint16_t b1, uint16_t b2)
        {
            for (size_t n = 0; n + 1 < combos.size(); n += 2)
                if (combos[n] == b1 && combos[n + 1] == b2) return static_cast<uint16_t>(n);
            uint16_t base = static_cast<uint16_t>(combos.size());
            combos.push_back(b1);
            combos.push_back(b2);
            return base;
        }

        // Park a level>0 submesh by zeroing its geometry (keep boneCount >= 1: native finalize divides the
        // budget by every submesh boneCount). Clamp a drawn submesh's boneCount to the SM3 ceiling and the
        // boneCombos bounds. A zero-geometry section is marked bad.
        void FixSubmeshes(sdk::M2Header* md, M2SkinProfile* skin, std::vector<uint8_t>& badSubmesh)
        {
            badSubmesh.assign(skin->submeshCount, 0);
            for (uint32_t i = 0; i < skin->submeshCount; ++i)
            {
                auto* s = &skin->submeshes[i];
                if (s->level > 0)
                {
                    s->level = 0; s->vertexStart = 0; s->vertexCount = 0; s->indexStart = 0;
                    s->indexCount = 0; s->boneComboIndex = 0; s->centerBoneIndex = 0;
                }

                if (s->indexCount == 0)
                {
                    if (s->boneCount < 1) s->boneCount = 1;
                    badSubmesh[i] = 1;
                }
                else
                {
                    uint16_t cap = bs::kMaxBonesPerDraw;
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

        // The < kSourceShaderMin shaderId tail: blend-bit decode + texUnitLookup synthesis, applied in
        // place to a batch that already holds the down-converted shaderId.
        void DecodeBlendBits(sdk::M2Batch* b, uint16_t shaderId, uint16_t textureCount,
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

        // One Source env batch splits into a primary plus a follower (a 2nd render pass over the SAME
        // geometry). The follower copies the primary, then carries material-layer 1 and renderflags index
        // +1 so the engine binds it as the layered 2nd pass over the diffuse below it. Returns false if
        // this low code is not an env split (caller falls through to the in-place decode).
        bool EnvSplit(uint16_t low, uint16_t shaderId, sdk::M2Batch primary, uint32_t nTransparencyLookup,
                      std::vector<sdk::M2Batch>& out, std::vector<int16_t>& texUnitLookup,
                      std::vector<uint16_t>& blendOverride)
        {
            sdk::M2Batch follower = primary;
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

        // Down-convert one batch into 'piece' (1 batch, or primary+follower for an env split). Does not
        // touch skinSectionIndex; the caller re-points it per target sub-section.
        void DownConvertBatch(sdk::M2Batch b, uint32_t nTransparencyLookup, std::vector<sdk::M2Batch>& piece,
                              std::vector<int16_t>& texUnitLookup, std::vector<uint16_t>& blendOverride)
        {
            uint16_t shaderId = b.shaderId;
            uint16_t textureCount = b.textureCount;
            b.flags &= 0x10;

            if (shaderId >= sdk::kSourceShaderMin)
            {
                // Source shader-effect indices [0..2] are Diffuse_T1_Env effects the engine renders
                // natively, re-based to engine index = sourceIdx+1 (index 0 is "no shader"). Emit ONE
                // 2-texture batch (T1 + env coord); do NOT route these through the EnvSplit heuristic.
                uint16_t sourceIdx = shaderId & 0x7fff;
                if (sourceIdx <= 2)
                {
                    b.shaderId               = static_cast<uint16_t>(sdk::kSourceShaderMin | (sourceIdx + 1));
                    b.textureCount           = 2;
                    b.textureCoordComboIndex = LookupPair(texUnitLookup, 0, -1);
                    b.flags                 &= 0x10;
                    piece.push_back(b);
                    return;
                }

                uint16_t low = shaderId & 0xFF;
                if (EnvSplit(low, shaderId, b, nTransparencyLookup, piece, texUnitLookup, blendOverride))
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

            if (shaderId < sdk::kSourceShaderMin)
                DecodeBlendBits(&b, shaderId, textureCount, texUnitLookup, blendOverride);
            else
                b.textureCount = textureCount < 2 ? textureCount : 2;

            piece.push_back(b);
        }

        // Build the down-converted batch array. Each original batch is processed once, then emitted for
        // every sub-section its original submesh became (skinSectionIndex re-pointed). A batch on a bad
        // section is reduced to a no-draw batch. With no split, splitMap is empty and every batch maps 1:1.
        void FixTexUnits(M2SkinProfile* skin, const std::vector<uint8_t>& badSubmesh,
                         const std::vector<bs::SplitRun>& splitMap, std::vector<sdk::M2Batch>& out,
                         std::vector<int16_t>& texUnitLookup, std::vector<uint16_t>& blendOverride,
                         uint32_t nTransparencyLookup)
        {
            out.reserve(skin->batchCount);
            for (uint32_t i = 0; i < skin->batchCount; ++i)
            {
                sdk::M2Batch b = skin->batches[i];

                bs::SplitRun run{ b.skinSectionIndex, 1 };
                if (b.skinSectionIndex < splitMap.size()) run = splitMap[b.skinSectionIndex];

                std::vector<sdk::M2Batch> piece;
                DownConvertBatch(b, nTransparencyLookup, piece, texUnitLookup, blendOverride);

                for (uint16_t s = 0; s < run.count; ++s)
                {
                    uint16_t sectionIdx = static_cast<uint16_t>(run.first + s);
                    bool bad = sectionIdx < badSubmesh.size() && badSubmesh[sectionIdx];
                    for (const sdk::M2Batch& p : piece)
                    {
                        sdk::M2Batch nb = p;
                        nb.skinSectionIndex = sectionIdx;
                        if (bad) nb.shaderId = 0x8000;
                        out.push_back(nb);
                    }
                }
            }
        }

        // Clamp a Source material blend mode the Client blend table cannot index (modes 0..6) to Add (4)
        // and strip flags above bit 5. Mesh uses Add (4) rather than the BlendAdd remap the particle path
        // uses: premultiplied "over" is too strong on mesh materials.
        void FixRenderFlags(sdk::M2Header* md)
        {
            if (!md->materials.count) return;
            auto* mats = reinterpret_cast<uint16_t*>(md->materials.offset);
            for (uint32_t i = 0; i < md->materials.count; ++i)
            {
                uint16_t& flag  = mats[i * 2 + 0];
                uint16_t& blend = mats[i * 2 + 1];
                if (blend > 6) { blend = 4; flag |= 0x5; }
                flag &= 0x1F;
            }
        }

        void RebuildSkinMaterials(sdk::M2Header* md, M2SkinProfile* skin, const char* name)
        {
            if (skin->batchCount > kMaxBatches)
            {
                WLOG_WARN("M2: '%s' skin batchCount=%u exceeds cap, clamping", name, skin->batchCount);
                skin->batchCount = kMaxBatches;
            }

            std::vector<uint8_t>      badSubmesh;
            std::vector<int16_t>      texUnitLookup;
            std::vector<uint16_t>     blendOverride;
            std::vector<sdk::M2Batch> batches;
            uint32_t nTransparencyLookup = md->textureWeightCombos.count;

            std::vector<bs::SplitSection> sections;
            std::vector<bs::SplitRun>     splitMap;
            uint32_t splitCount = 0;
            if (bs::SplitSubmeshes(md, skin, sections, splitMap, splitCount, name) && splitCount > 0)
                WLOG_INFO("M2: '%s' bone-splitter produced %u extra sub-draw(s)", name, splitCount);

            FixSubmeshes(md, skin, badSubmesh);
            FixTexUnits(skin, badSubmesh, splitMap, batches, texUnitLookup, blendOverride, nTransparencyLookup);

            // Commit the grown batch array BEFORE native finalize sizes its parallel block from
            // skin->batchCount. The file-mapped arrays are never per-array freed, so the new buffer is
            // leaked for the model's lifetime (same pattern as the header arrays below).
            if (!batches.empty())
            {
                auto* buf = static_cast<sdk::M2Batch*>(malloc(batches.size() * sizeof(sdk::M2Batch)));
                memcpy(buf, batches.data(), batches.size() * sizeof(sdk::M2Batch));
                skin->batches    = buf;
                skin->batchCount = static_cast<uint32_t>(batches.size());
            }

            if (!texUnitLookup.empty())
            {
                auto* buf = static_cast<int16_t*>(malloc(texUnitLookup.size() * sizeof(int16_t)));
                memcpy(buf, texUnitLookup.data(), texUnitLookup.size() * sizeof(int16_t));
                md->textureUnitLookup.count  = static_cast<uint32_t>(texUnitLookup.size());
                md->textureUnitLookup.offset = reinterpret_cast<uint32_t>(buf);
            }

            if (!blendOverride.empty())
            {
                auto* buf = static_cast<uint16_t*>(malloc(blendOverride.size() * sizeof(uint16_t)));
                memcpy(buf, blendOverride.data(), blendOverride.size() * sizeof(uint16_t));
                md->textureCombinerCombos.count  = static_cast<uint32_t>(blendOverride.size());
                md->textureCombinerCombos.offset = reinterpret_cast<uint32_t>(buf);
                md->globalFlags |= sdk::kFlagUseTextureCombinerCombos;
            }

            FixRenderFlags(md);
        }

        // ---- ribbon material-index expand -------------------------------------------------------------

        // Per ribbon: if materialIndices is shorter than textureIndices, allocate a textureIndices.count
        // array filled with materialIndices[0] and repoint the ribbon's materialIndices. The native ribbon
        // setup reads materialIndices[i] for i in [0, textureIndices.count); a short array OOBs. Repeating
        // [0] is exactly what a 1-material ribbon means. Operates on raw post-de-reloc pointers.
        void ExpandRibbonMaterialsImpl(uint8_t* ribbonArray, uint32_t ribbonCount)
        {
            for (uint32_t i = 0; i < ribbonCount; ++i)
            {
                auto* rb = reinterpret_cast<sdk::M2Ribbon*>(ribbonArray + i * sdk::kRibbonStrideClient);
                uint32_t texCount = rb->textureIndices.count;
                uint32_t matCount = rb->materialIndices.count;
                if (matCount >= texCount || matCount == 0 || rb->materialIndices.offset == 0)
                    continue;

                uint16_t first = *reinterpret_cast<uint16_t*>(rb->materialIndices.offset);
                auto* buf = static_cast<uint16_t*>(malloc(texCount * sizeof(uint16_t)));
                if (!buf) continue;
                for (uint32_t m = 0; m < texCount; ++m) buf[m] = first;

                rb->materialIndices.count  = texCount;
                rb->materialIndices.offset = reinterpret_cast<uint32_t>(buf);
            }
        }

        // Skin finalize: the skin is attached + pointer-fixed and the header is live, BEFORE the native
        // shader-id passes run. Rebuild the material contract a Source skin lacks so the first pass does
        // not NULL-deref header.textureUnitLookup.
        void __fastcall FinalizeDetour(void* self)
        {
            ModelView model(self);
            sdk::M2Header* md = model.fileData();
            M2SkinProfile* skin = model.skin();
            if (md && skin && IsSourceModel(md))
                RebuildSkinMaterials(md, skin, model.name());
            g_finalizeOriginal(self);
        }

        // Ribbon de-relocator: after the native pass turns each ribbon's textureIndices/materialIndices
        // into raw pointers, expand any materialIndices shorter than textureIndices so every layer reads
        // materialIndices[0]. param_4 = &header.ribbonEmitters; after the original, param_4[0] = count,
        // param_4[1] = raw ribbon-array pointer.
        int __cdecl RibbonDeRelocDetour(int base, unsigned int fileSize, int ctx, unsigned int* ribbons)
        {
            int result = g_ribbonDeRelocOriginal(base, fileSize, ctx, ribbons);
            if (result != 0 && ribbons && ribbons[0] != 0 && ribbons[1] != 0)
            {
                __try { ExpandRibbonMaterialsImpl(reinterpret_cast<uint8_t*>(ribbons[1]), ribbons[0]); }
                __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
            return result;
        }

        // Model init: the host serves a target-shaped model, so the native parse handles it directly.
        // The hook only records that a Source model loaded (for diagnostics); all structural work is done
        // on the host and the material rebuild runs at skin finalize.
        int __fastcall InitDetour(void* self)
        {
            ModelView model(self);
            sdk::M2Header* md = model.fileData();
            uint32_t version = (md && md->magic == sdk::kMagicMD20) ? md->version : 0;

            int result = g_initOriginal(self);

            if (version > sdk::kVersion)
                WLOG_INFO("M2: '%s' version=%u result=%d", model.name(), version, result);
            return result;
        }
    }

    void Install()
    {
        // Widen the native version gate (target version only) to accept the Source inner versions.
        mem::Fill(reinterpret_cast<void*>(off::kVersionGateInit), 0x90, 6); // NOP the version-too-high branch
        const uint8_t jmpShort = 0xEB;
        mem::Patch(reinterpret_cast<void*>(off::kVersionGateAnim), &jmpShort, 1); // turn the anim branch into a jump

        // Force DXT texture uploads through the mip-copy path. The zero-copy fast path aliases the transient
        // BLP file buffer, which is freed/recycled before the upload reads it -> a large host-served DXT
        // texture (1024x1024) uploads from a stale pointer and faults. Native-safe, resolution preserved.
        mem::Fill(reinterpret_cast<void*>(tex::kDxtZeroCopyGate), 0x90, tex::kDxtZeroCopyGateLen);

        hook::Install("M2::Init", off::kInit, &InitDetour,
                      reinterpret_cast<void**>(&g_initOriginal));
        hook::Install("M2::FinalizeSkin", off::kFinalizeSkin, &FinalizeDetour,
                      reinterpret_cast<void**>(&g_finalizeOriginal));
        hook::Install("M2::RibbonDeRelocate", off::kRibbonDeRelocate, &RibbonDeRelocDetour,
                      reinterpret_cast<void**>(&g_ribbonDeRelocOriginal));

        WLOG_INFO("M2: loader installed");
    }
}
