#include "Features/M2/Modern.hpp"

#include "Core/Logger.hpp"

#include <windows.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace sdk = wraith::m2;

namespace
{
    constexpr float kCameraFov = 0.7853982f;   // 45deg, substituted for the fov float modern cameras drop

    // Compact each 0x74 modern camera onto the 0x64 contract in place. dst stride (0x64) < src stride
    // (0x74), so the forward walk never overwrites a camera before it is read.
    void CompactCameras(sdk::M2Header* md, float defaultFov)
    {
        if (!md->cameras.count || !md->cameras.offset) return;
        uint8_t* arr = md->base() + md->cameras.offset;
        for (uint32_t i = 0; i < md->cameras.count; ++i)
        {
            auto* src = reinterpret_cast<sdk::modern::M2Camera*>(arr + i * sizeof(sdk::modern::M2Camera));
            auto* dst = reinterpret_cast<sdk::M2Camera*>(arr + i * sizeof(sdk::M2Camera));

            uint32_t type     = src->type;
            float    farClip  = src->farClip;
            float    nearClip = src->nearClip;
            memmove(&dst->body, &src->body, sizeof(sdk::M2CameraBody));
            dst->type     = type;
            dst->fov      = defaultFov;
            dst->farClip  = farClip;
            dst->nearClip = nearClip;
        }
    }

    // Compact each modern particle emitter (0x1ec) onto the 264 emitter (0x1dc) in place. The native
    // de-relocator strides by 0x1dc, so an un-slid modern body parses later emitters from the wrong bytes
    // and the load fails its sub-array bounds-check. dst stride < src stride, so the forward walk never
    // overwrites an emitter before it is read.
    // Assumes the 16 added Cata+ bytes sit at the tail (interior layout unverified); the leading 0x1dc
    // bytes carry every field the engine reads.
    void CompactParticles(sdk::M2Header* md)
    {
        if (!md->particleEmitters.count || !md->particleEmitters.offset) return;
        uint8_t* arr = md->base() + md->particleEmitters.offset;
        for (uint32_t i = 0; i < md->particleEmitters.count; ++i)
        {
            uint8_t* src = arr + i * sdk::modern::kParticleStrideModern;
            uint8_t* dst = arr + i * sdk::modern::kParticleStride264;
            if (dst != src) memmove(dst, src, sdk::modern::kParticleStride264);

            // A multi-texture emitter packs three 5-bit texture ids into textureId; 264 reads the field flat
            // and uses it directly into header.textures, so the packed value overruns the texture-handle
            // table (a wild AddRef in the native particle setup). Collapse it to the first id.
            uint32_t flags  = *reinterpret_cast<uint32_t*>(dst + 0x4);
            uint16_t* texId = reinterpret_cast<uint16_t*>(dst + sdk::modern::kParticleTextureIdOff);
            if (flags & sdk::modern::kParticleFlagMultiTex)
                *texId &= sdk::modern::kParticleTextureIdMask;
            // Robustness: any textureId past the table (malformed or unforeseen packing) parks at id 0
            // rather than letting the native loop deref a wild texture-handle slot.
            if (md->textures.count && *texId >= md->textures.count)
                *texId = 0;

            // blendingType @+0x28: the 264 M2-blend table is stride-7 (modes 0..6), so Cata+ BlendAdd (7)
            // indexes out of its row. BlendAdd = (src ONE, dst INV_SRC_ALPHA) = the engine's M2 mode 3
            // (device blend index 10), so 7 maps to 3 exactly; any other unknown mode >7 falls back to Add (4).
            uint8_t* blend = dst + sdk::modern::kParticleBlendOff;
            if (*blend == 7)      *blend = 3;            // faithful BlendAdd (device blend index 10)
            else if (*blend > 7)  *blend = 4;

            // Flipbook: wrap the head/tail cell keyframes into [0, rows*cols). Cata wraps the sampled cell
            // by the atlas cell count; 264 does not, so a keyframe >= cols maps to row >= 1 and samples V
            // off the atlas. Wrapping at load keeps every keyframe in-atlas.
            uint16_t rows = *reinterpret_cast<uint16_t*>(dst + sdk::modern::kParticleTexRowsOff);
            uint16_t cols = *reinterpret_cast<uint16_t*>(dst + sdk::modern::kParticleTexColsOff);
            int16_t cells = static_cast<int16_t>(rows * cols);
            if (cells > 1)
            {
                const uint32_t cellTracks[2] = { sdk::modern::kParticleHeadCellOff,
                                                 sdk::modern::kParticleTailCellOff };
                for (uint32_t track : cellTracks)
                {
                    // M2PartTrack keys = M2Array{count,ofs} at FBlock+0x8; ofs is md-relative (pre-de-reloc).
                    uint32_t keyCount = *reinterpret_cast<uint32_t*>(dst + track + 0x8);
                    uint32_t keyOfs   = *reinterpret_cast<uint32_t*>(dst + track + 0xc);
                    if (keyCount == 0 || keyCount > 0x1000 || keyOfs == 0) continue;
                    int16_t* keys = reinterpret_cast<int16_t*>(md->base() + keyOfs);
                    for (uint32_t k = 0; k < keyCount; ++k)
                        if (keys[k] >= cells) keys[k] %= cells;
                }
            }

            // Compressed gravity (flags 0x800000): the gravity M2Track keys are packed
            // {int8 x, int8 y, int16 z}, not floats. Decompress each to the plain 264 float scalar
            // (+downward) and clear the flag; otherwise the 4 packed bytes read as a float can form a NaN
            // and poison the particle position. Two-level track: val outer {count,ofs} at emitter+0x90/+0x94
            // (md-relative, pre-de-reloc); each outer element is an inner M2Array whose ofs points at the
            // 4-byte keys. The keys live past the emitter array, untouched by the slide.
            if (flags & sdk::modern::kParticleFlagCompressedGravity)
            {
                uint32_t outerCount = *reinterpret_cast<uint32_t*>(dst + sdk::modern::kParticleGravityValCountOff);
                uint32_t outerOfs   = *reinterpret_cast<uint32_t*>(dst + sdk::modern::kParticleGravityValOfsOff);
                if (outerOfs && outerCount && outerCount <= 0x1000)
                {
                    uint8_t* outer = md->base() + outerOfs;
                    for (uint32_t o = 0; o < outerCount; ++o)
                    {
                        uint32_t innerCount = *reinterpret_cast<uint32_t*>(outer + o * 8 + 0x0);
                        uint32_t innerOfs   = *reinterpret_cast<uint32_t*>(outer + o * 8 + 0x4);
                        if (!innerOfs || !innerCount || innerCount > 0x1000) continue;
                        uint8_t* keys = md->base() + innerOfs;
                        for (uint32_t k = 0; k < innerCount; ++k)
                        {
                            uint8_t* key = keys + k * 4;
                            float dx = static_cast<int8_t>(key[0]) / 128.0f;
                            float dy = static_cast<int8_t>(key[1]) / 128.0f;
                            int16_t zraw = *reinterpret_cast<int16_t*>(key + 2);
                            float planar = dx * dx + dy * dy;
                            float zc  = std::sqrt(planar < 1.0f ? 1.0f - planar : 0.0f);
                            float mag = zraw * sdk::modern::kParticleGravityMagUnit;
                            if (mag < 0.0f) { zc = -zc; mag = -mag; }
                            *reinterpret_cast<float*>(key) = -(zc * mag);   // +downward 264 scalar
                        }
                    }
                }
                *reinterpret_cast<uint32_t*>(dst + 0x4) &= ~sdk::modern::kParticleFlagCompressedGravity;
            }
        }
    }

    // Slide each modern ribbon emitter onto the 264 stride and clamp its texture/material indices into the
    // header tables. The native ribbon de-relocator and the ribbon draw both stride by kRibbonStride264
    // (0xb0); modern ribbons keep that exact stride because the Cata+ tail
    // (priorityPlane/ribbonColorIndex/textureTransformLookupIndex) occupies the 264 layout's trailing
    // padding (m2::M2Ribbon at 0xac..0xb0), so the slide is a no-op while strideModern == stride264. It is
    // written against the named strides so a version that grows the ribbon body only edits kRibbonStrideModern.
    // Forward walk is safe whenever dst stride <= src stride: a destination element never overwrites a
    // source element not yet read.
    //
    // The textureIndices/materialIndices M2Arrays are still file-relative (count,offset) pairs here; the
    // slide leaves their fields in place at +0x14/+0x1c so the de-relocator's pointer-fix reads them
    // correctly. The draw case derefs materialIndices[0] flat into header.materials, and the ribbon
    // emitter binds textureIndices[i] into header.textures; an out-of-range entry (e.g. an unforeseen
    // modern packing) would index past those tables, so any index >= the table count is parked at 0. This is
    // the ribbon analog of the particle textureId overrun guard.
    void CompactRibbons(sdk::M2Header* md)
    {
        if (!md->ribbonEmitters.count || !md->ribbonEmitters.offset) return;
        static_assert(sdk::modern::kRibbonStrideModern >= sdk::modern::kRibbonStride264,
                      "ribbon forward slide requires dst stride <= src stride");

        uint8_t* arr = md->base() + md->ribbonEmitters.offset;
        for (uint32_t i = 0; i < md->ribbonEmitters.count; ++i)
        {
            uint8_t* src = arr + i * sdk::modern::kRibbonStrideModern;
            uint8_t* dst = arr + i * sdk::modern::kRibbonStride264;
            if (dst != src) memmove(dst, src, sdk::modern::kRibbonStride264);

            auto* rb = reinterpret_cast<sdk::M2Ribbon*>(dst);
            if (md->textures.count)
            {
                auto* texIdx = reinterpret_cast<uint16_t*>(md->base() + rb->textureIndices.offset);
                for (uint32_t t = 0; t < rb->textureIndices.count; ++t)
                    if (texIdx[t] >= md->textures.count) texIdx[t] = 0;
            }
            if (md->materials.count)
            {
                auto* matIdx = reinterpret_cast<uint16_t*>(md->base() + rb->materialIndices.offset);
                for (uint32_t m = 0; m < rb->materialIndices.count; ++m)
                    if (matIdx[m] >= md->materials.count) matIdx[m] = 0;
            }
        }
    }

    // Per ribbon: if materialIndices is shorter than textureIndices, allocate a textureIndices.count u16
    // array filled with materialIndices[0] and repoint the ribbon's materialIndices (count+ptr) at it. The
    // native ribbon setup reads materialIndices[i] for i in [0, textureIndices.count); a short array OOBs.
    // Repeating [0] is exactly what a 1-material ribbon means (Legion's whole-ribbon material). Operates on
    // raw pointers (post-de-reloc): rb->materialIndices.offset / textureIndices.offset hold raw addresses.
    // The new array is leaked for the model's lifetime (same pattern as RebuildSkinMaterials).
    void ExpandRibbonMaterialsImpl(uint8_t* ribbonArray, uint32_t ribbonCount)
    {
        for (uint32_t i = 0; i < ribbonCount; ++i)
        {
            auto* rb = reinterpret_cast<sdk::M2Ribbon*>(ribbonArray + i * sdk::modern::kRibbonStride264);
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

    // Index of the first sequence whose id equals anim, or -1.
    int16_t AnimationIndex(sdk::M2Sequence* seqs, uint32_t count, uint16_t anim)
    {
        for (uint32_t i = 0; i < count; ++i)
            if (seqs[i].id == anim) return static_cast<int16_t>(i);
        return -1;
    }

    // Point sequenceLookup[new_id] at new_pos if it still holds old_pos, else rewrite the first lookup
    // entry that holds old_pos. A -1 old_pos (AnimationIndex miss) matches nothing and is a no-op.
    void ReplaceAnimLookup(int16_t* lookup, uint32_t lookupCount, int16_t oldPos, uint16_t newId,
                           int16_t newPos)
    {
        if (newId < lookupCount && lookup[newId] == oldPos)
        {
            lookup[newId] = newPos;
            return;
        }
        for (uint32_t i = 0; i < lookupCount; ++i)
            if (lookup[i] == oldPos) { lookup[i] = newPos; break; }
    }

    // Two transforms over the file-relative sequence array:
    //  - EVERY sequence: mask blendTime to its low u16. Cata+ split that u32 into blendTimeIn|blendTimeOut;
    //    read whole by the 264 engine it is a huge blend duration so transitions never complete (stuck or
    //    sliding animations).
    //  - id > 505 (WotLK max anim id): remap a curated swim/jump set to a 264 id and patch sequenceLookup so
    //    the engine still resolves them.
    void FixAnimations(sdk::M2Header* md)
    {
        if (!md->sequences.count || !md->sequences.offset) return;
        auto* seqs = reinterpret_cast<sdk::M2Sequence*>(md->base() + md->sequences.offset);
        uint32_t count = md->sequences.count;

        int16_t* lookup     = nullptr;
        uint32_t lookupCount = 0;
        if (md->sequenceLookup.count && md->sequenceLookup.offset)
        {
            lookup      = reinterpret_cast<int16_t*>(md->base() + md->sequenceLookup.offset);
            lookupCount = md->sequenceLookup.count;
        }

        for (uint32_t i = 0; i < count; ++i)
        {
            uint16_t id = seqs[i].id;
            if (id > 505)
            {
                uint16_t anim = id;
                switch (id)
                {
                case 564: anim = 37;  break;
                case 548: anim = 41;  break;
                case 556: anim = 42;  break;
                case 552: anim = 43;  break;
                case 554: anim = 44;  break;
                case 562: anim = 45;  break;
                case 572: anim = 39;  break;
                case 574: anim = 187; break;
                }
                if (id != anim && lookup)
                {
                    ReplaceAnimLookup(lookup, lookupCount, AnimationIndex(seqs, count, anim), anim,
                                      static_cast<int16_t>(i));
                    seqs[i].id = anim;
                }
            }

            seqs[i].blendTime &= 0xFFFF;
        }
    }

    // ---- 264 material contract rebuild ----------------------------------------------------------------
    // A modern .skin encodes each batch's material in its own shaderId (Cata+ scheme) and leaves the
    // header textureUnitLookup (0x88) empty. The WotLK skin's first shader-id pass instead indexes
    // header.textureUnitLookup[batch.texCoordCombo]; with that array NULL it derefs 0x0. The transform
    // re-derives the 264 per-batch fields (shaderId/texCoordCombo/textureCount) and synthesises the header
    // textureUnitLookup + textureCombinerCombos the native passes expect.

    // Append entry, returning the index of an existing single-entry match or the new slot.
    uint16_t LookupSingle(std::vector<int16_t>& lookup, int16_t v)
    {
        for (size_t n = 0; n < lookup.size(); ++n)
            if (lookup[n] == v) return static_cast<uint16_t>(n);
        lookup.push_back(v);
        return static_cast<uint16_t>(lookup.size() - 1);
    }

    // Append an adjacent pair, returning the base index. Reuses an overlapping tail so the two consecutive
    // entries [base], [base+1] equal a, b.
    uint16_t LookupPair(std::vector<int16_t>& lookup, int16_t a, int16_t b)
    {
        for (size_t n = 0; n + 1 < lookup.size(); ++n)
            if (lookup[n] == a && lookup[n + 1] == b) return static_cast<uint16_t>(n);
        size_t sz = lookup.size();
        if (sz > 1 && lookup[sz - 1] == a)
        {
            lookup.push_back(b);
            return static_cast<uint16_t>(sz - 1);
        }
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

    // SM3 vertex-constant ceiling: the bone palette starts at c31, 3 registers per bone matrix, so
    // (256 - 31) / 3 = 75 bones per draw.
    // staging buffer and corrupts the adjacent state-descriptor table (ERROR #132).
    constexpr uint16_t kMaxBonesPerDraw = 75;

    // A level>0 submesh is a (level<<16|id) sub-batch the 264 engine cannot draw. Park it by zeroing its
    // geometry and marking badSubmesh so its batch is skipped, but KEEP boneCount/boneInfluences intact:
    // native FinalizeSkin divides boneCountMax by every submesh boneCount (parked or not), so a zeroed
    // boneCount is a divide-by-zero. A drawn (level 0) submesh gets boneCount clamped to the SM3 ceiling
    // and to the boneCombos array bounds (>=1), plus a >=1 bone-influence floor.
    void FixSubmeshes(sdk::M2Header* md, sdk::M2SkinProfile* skin, std::vector<uint8_t>& badSubmesh)
    {
        badSubmesh.assign(skin->submeshCount, 0);
        for (uint32_t i = 0; i < skin->submeshCount; ++i)
        {
            auto* s = &skin->submeshes[i];
            if (s->level > 0)
            {
                s->level           = 0;
                s->vertexStart     = 0;
                s->vertexCount     = 0;
                s->indexStart      = 0;
                s->indexCount      = 0;
                s->boneComboIndex  = 0;
                s->centerBoneIndex = 0;
                badSubmesh[i] = 1;
            }
            else
            {
                uint16_t cap = kMaxBonesPerDraw;
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

    // The < 0x8000 shaderId tail: blend-bit decode + texUnitLookup synthesis, applied in place to a batch
    // that already holds the down-converted shaderId. Used by the normal path and (after the env split
    // forces single-texture) the follower's primary.
    void DecodeBlendBits(sdk::M2Batch* b, uint16_t shaderId, uint16_t textureCount,
                         std::vector<int16_t>& texUnitLookup, std::vector<uint16_t>& blendOverride)
    {
        uint16_t blend1 = (shaderId >> 4) & 0x7;
        uint16_t blend2 = shaderId & 0x7;

        bool twoTex = textureCount > 1 && (shaderId & 0x4000) && blend1 != 0 && blend2 != 0;
        uint16_t shaderToSave = 0;
        if (twoTex)
            shaderToSave = BlendOverride(blendOverride, blend1, blend2);
        else
            textureCount = 1;

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
            if (shaderId & 0x80)
            {
                t0 = -1;
                t1 = (shaderId & 0x8) ? -1 : 0;
            }
            else
            {
                t0 = 0;
                t1 = (shaderId & 0x8) ? -1 : ((shaderId & 0x4000) ? 1 : 0);
            }
            b->textureCoordComboIndex = LookupPair(texUnitLookup, t0, t1);
        }

        b->textureCount = textureCount < 2 ? textureCount : 2;
    }

    // One modern env batch splits into a primary plus a follower (a 2nd render pass over the SAME
    // geometry). The follower copies the primary's fields, then carries material-layer 1 and renderflags
    // index +1 so the engine binds it as the layered 2nd pass over the diffuse below it. Three case
    // families differ only in blend override + tex-coord lookup, keyed off the shaderId >= 0x8000 code.
    // out gets primary then follower. Returns false if this low code is not an env split (caller falls
    // through to the in-place decode).
    bool EnvSplit(uint16_t low, uint16_t shaderId, sdk::M2Batch primary, uint32_t nTransparencyLookup,
                  std::vector<sdk::M2Batch>& out, std::vector<int16_t>& texUnitLookup,
                  std::vector<uint16_t>& blendOverride)
    {
        sdk::M2Batch follower = primary;
        follower.materialIndex  = static_cast<uint16_t>(primary.materialIndex + 1);
        follower.materialLayer  = 1;
        follower.textureCount   = 1;

        switch (low)
        {
        case 0: case 3: case 9: case 17: case 24:
        {
            primary.textureCount = 2;
            uint16_t blendIdx = BlendOverride(blendOverride, 1, 4);
            uint16_t tc = LookupPair(texUnitLookup, 0, -1);
            primary.shaderId               = blendIdx;
            primary.textureCoordComboIndex = tc;
            follower.shaderId               = blendIdx;
            follower.textureCoordComboIndex = tc;
            out.push_back(primary);
            out.push_back(follower);
            return true;
        }
        case 1: case 15:
        {
            primary.textureCount   = 1;
            primary.shaderId       = 0;   // no blend override on this family
            follower.shaderId      = 0;
            follower.textureComboIndex = static_cast<uint16_t>(primary.textureComboIndex + 1);
            // transparency-combo only advances when a +1 slot exists in the header weight lookup
            if (static_cast<uint32_t>(primary.textureWeightComboIndex) + 1 < nTransparencyLookup)
                follower.textureWeightComboIndex = static_cast<uint16_t>(primary.textureWeightComboIndex + 1);

            int16_t t1 = (shaderId == 0x8001) ? -1 : 1;
            uint16_t tc = LookupPair(texUnitLookup, 0, t1);
            primary.textureCoordComboIndex  = tc;
            follower.textureCoordComboIndex = static_cast<uint16_t>(tc + 1);
            out.push_back(primary);
            out.push_back(follower);
            return true;
        }
        case 2:
        {
            uint16_t blendIdx = BlendOverride(blendOverride, 1, 3);
            uint16_t tc = LookupPair(texUnitLookup, 0, -1);
            primary.shaderId               = blendIdx;
            primary.textureCoordComboIndex = tc;
            follower.shaderId               = blendIdx;
            follower.textureCoordComboIndex = tc;
            out.push_back(primary);
            out.push_back(follower);
            return true;
        }
        default:
            return false;
        }
    }

    // Builds the down-converted batch array. Most batches map 1:1; env effects split into a primary +
    // follower, so the array grows. The result is committed to skin->batches / skin->batchCount at the
    // FinalizeSkin entry, before the native passes size their parallel +0x188 block from skin->batchCount,
    // so the grow is seen and the block is sized correctly.
    void FixTexUnits(sdk::M2SkinProfile* skin, const std::vector<uint8_t>& badSubmesh,
                     std::vector<sdk::M2Batch>& out, std::vector<int16_t>& texUnitLookup,
                     std::vector<uint16_t>& blendOverride, uint32_t nTransparencyLookup)
    {
        out.reserve(skin->batchCount);
        for (uint32_t i = 0; i < skin->batchCount; ++i)
        {
            sdk::M2Batch b = skin->batches[i];   // by value: edits build the new array, originals untouched
            uint16_t shaderId = b.shaderId;

            if (b.skinSectionIndex < badSubmesh.size() && badSubmesh[b.skinSectionIndex])
            {
                b.shaderId = 0x8000;   // pass-1 skips 0x8000; the parked submesh draws nothing
                out.push_back(b);
                continue;
            }

            uint16_t textureCount = b.textureCount;
            b.flags &= 0x10;

            if (shaderId >= sdk::modern::kShaderMin)
            {
                // Modern shader-effect indices [0..2] are Diffuse_T1_Env env effects the engine renders
                // natively, re-based to engine index = legionIdx+1 (index 0 is reserved for "no shader").
                // Emit ONE 2-texture Diffuse_T1_Env batch (T1 + env coord), matching the native env merges;
                // do NOT route these through the EnvSplit heuristic.
                uint16_t legionIdx = shaderId & 0x7fff;
                if (legionIdx <= 2)
                {
                    b.shaderId               = static_cast<uint16_t>(sdk::modern::kShaderMin | (legionIdx + 1));
                    b.textureCount           = 2;
                    b.textureCoordComboIndex = LookupPair(texUnitLookup, 0, -1);
                    b.flags                 &= 0x10;
                    out.push_back(b);
                    continue;
                }

                uint16_t low = shaderId & 0xFF;
                if (EnvSplit(low, shaderId, b, nTransparencyLookup, out, texUnitLookup, blendOverride))
                    continue;
                switch (low)
                {
                case 5: case 8: case 10: case 12: case 16: case 23:
                    shaderId = 0; textureCount = 1; break;
                case 21:  // Combiners_Mod_Mod
                    shaderId = 0x4011; textureCount = 2; break;
                default:  // Combiners_Mod
                    shaderId = 0x0010; textureCount = 1; break;
                }
            }

            if (shaderId < sdk::modern::kShaderMin)
                DecodeBlendBits(&b, shaderId, textureCount, texUnitLookup, blendOverride);
            else
                b.textureCount = textureCount < 2 ? textureCount : 2;

            out.push_back(b);
        }
    }

    // Clamp a Cata+ material blend mode the 264 blend table cannot index (it has modes 0..6) to Add (4),
    // and strip flags above bit 5. The mesh path uses Add (4) rather than the faithful BlendAdd remap
    // (7->3 = device blend index 10) that the particle path uses: premultiplied "over" is too strong on
    // mesh materials.
    void FixRenderFlags(sdk::M2Header* md)
    {
        if (!md->materials.count) return;
        auto* mats = reinterpret_cast<uint16_t*>(md->materials.offset);  // raw pointer post-parse
        for (uint32_t i = 0; i < md->materials.count; ++i)
        {
            uint16_t& flag  = mats[i * 2 + 0];
            uint16_t& blend = mats[i * 2 + 1];
            if (blend > 6) { blend = 4; flag |= 0x5; }
            flag &= 0x1F;
        }
    }

    // A skin->batchCount past this is treated as malformed; the env split can up to double it, so cap the
    // commit well under any value that would overflow the native +0x188 sizing (batchCount*4).
    constexpr uint32_t kMaxBatches = 0x4000;

    void RebuildSkinMaterials(sdk::M2Header* md, sdk::M2SkinProfile* skin, const char* name)
    {
        if (skin->batchCount > kMaxBatches)
        {
            WLOG_WARN("MD21: '%s' skin batchCount=%u exceeds cap, skipping rebuild", name, skin->batchCount);
            return;
        }

        std::vector<uint8_t> badSubmesh;
        std::vector<int16_t> texUnitLookup;
        std::vector<uint16_t> blendOverride;
        std::vector<sdk::M2Batch> batches;
        uint32_t nTransparencyLookup = md->textureWeightCombos.count;   // transparency-lookup count (header +0x90)

        FixSubmeshes(md, skin, badSubmesh);
        FixTexUnits(skin, badSubmesh, batches, texUnitLookup, blendOverride, nTransparencyLookup);

        // Commit the grown batch array into an owned heap buffer and repoint the skin BEFORE the native
        // FinalizeSkin (g_finalizeOriginal) sizes its +0x188 block from skin->batchCount. skin->batches was
        // file-mapped memory the engine never per-array frees, so leaking the old pointer is fine; the new
        // buffer is leaked for the model's lifetime (same pattern as the header arrays below).
        if (!batches.empty())
        {
            auto* buf = static_cast<sdk::M2Batch*>(malloc(batches.size() * sizeof(sdk::M2Batch)));
            memcpy(buf, batches.data(), batches.size() * sizeof(sdk::M2Batch));
            skin->batches    = buf;
            skin->batchCount = static_cast<uint32_t>(batches.size());
        }

        // Commit the synthesised header arrays into owned heap buffers; post-parse the header array fields
        // are raw pointers, so the native passes read them directly. Leaked for the model's lifetime.
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
}

namespace wraith::features::m2::modern
{
    void ApplyFixups(sdk::M2Header* md, uint32_t /*slackBegin*/, uint32_t /*slackEnd*/)
    {
        CompactCameras(md, kCameraFov);
        CompactParticles(md);
        CompactRibbons(md);
        FixAnimations(md);
    }

    void RebuildMaterials(sdk::M2Header* md, sdk::M2SkinProfile* skin, const char* name)
    {
        RebuildSkinMaterials(md, skin, name);
    }

    // SEH-guarded: derefs raw post-de-reloc ribbon pointers; a malformed model must not crash Init.
    void ExpandRibbonMaterials(uint8_t* ribbonArray, uint32_t ribbonCount)
    {
        if (!ribbonArray || !ribbonCount) return;
        __try { ExpandRibbonMaterialsImpl(ribbonArray, ribbonCount); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
}
