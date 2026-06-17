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

#include "M2Translate.hpp"

#include <cmath>
#include <cstring>
#include <string>

// De-chunk the modern container into the model layout the native loader expects, then compact each
// Source record layout (cameras / particle emitters / ribbon emitters) onto the Client strides and
// normalize the Source-only sequence fields, working purely on the file bytes. The native parser walks
// the model by these strides, so an un-compacted Source body parses later records from the wrong bytes.
// All offsets here are model-relative (pre-parse), so every transform is a pure byte rewrite. It is
// DATA-GATED: keyed off the container magic and the inner-version range, so one path serves every Source
// build (the format only adds data; gating is by presence, not version).
namespace wraith::structure::m2
{
    namespace
    {
        uint32_t  Rd32(const uint8_t* p)        { uint32_t v; std::memcpy(&v, p, 4); return v; }
        void      Wr32(uint8_t* p, uint32_t v)  { std::memcpy(p, &v, 4); }
        uint16_t  Rd16(const uint8_t* p)        { uint16_t v; std::memcpy(&v, p, 2); return v; }
        void      Wr16(uint8_t* p, uint16_t v)  { std::memcpy(p, &v, 2); }

        constexpr float kCameraFov = 0.7853982f; // 45deg, substituted for the fov float Source cameras drop

        // Slide the inner model to the buffer start. The container header precedes a self-relative inner
        // model whose offsets need no patch; only the leading header bytes are dropped. Returns the inner
        // model size, or 0 if the buffer is not a container.
        uint32_t DeChunk(std::vector<uint8_t>& buf)
        {
            if (buf.size() < sizeof(ContainerHeader)) return 0;
            if (Rd32(buf.data()) != kMagicMD21) return 0;
            uint32_t innerSize = Rd32(buf.data() + 4);
            if (sizeof(ContainerHeader) + static_cast<size_t>(innerSize) > buf.size()) return 0;
            std::memmove(buf.data(), buf.data() + sizeof(ContainerHeader), innerSize);
            buf.resize(innerSize);
            return innerSize;
        }

        // Compact each Source camera (0x74) onto the Client camera (0x64) in place. dst stride < src
        // stride, so the forward walk never overwrites a camera before it is read. The dropped fov float
        // is replaced with a default.
        void CompactCameras(M2Header* md)
        {
            if (!md->cameras.count || !md->cameras.offset) return;
            uint8_t* arr = md->base() + md->cameras.offset;
            for (uint32_t i = 0; i < md->cameras.count; ++i)
            {
                auto* src = reinterpret_cast<M2CameraSource*>(arr + i * sizeof(M2CameraSource));
                auto* dst = reinterpret_cast<M2Camera*>(arr + i * sizeof(M2Camera));

                uint32_t type     = src->type;
                float    farClip  = src->farClip;
                float    nearClip = src->nearClip;
                std::memmove(&dst->body, &src->body, sizeof(M2CameraBody));
                dst->type     = type;
                dst->fov      = kCameraFov;
                dst->farClip  = farClip;
                dst->nearClip = nearClip;
            }
        }

        // Compact each Source particle emitter onto the Client emitter stride in place and normalize the
        // Source-only encodings. The native de-relocator strides by the Client value, so an un-slid Source
        // body parses later emitters from the wrong bytes and fails its sub-array bounds check. dst stride
        // < src stride, so the forward walk never overwrites an emitter before it is read. The added bytes
        // sit at the tail; the leading Client-stride bytes carry every field the engine reads.
        void CompactParticles(M2Header* md, uint32_t fileSize)
        {
            if (!md->particleEmitters.count || !md->particleEmitters.offset) return;
            uint8_t* arr = md->base() + md->particleEmitters.offset;
            for (uint32_t i = 0; i < md->particleEmitters.count; ++i)
            {
                uint8_t* src = arr + i * kParticleStrideSource;
                uint8_t* dst = arr + i * kParticleStrideClient;
                if (dst != src) std::memmove(dst, src, kParticleStrideClient);

                // A multi-texture emitter packs three 5-bit ids into textureId; the Client reads it flat
                // into header.textures, so the packed value overruns the texture-handle table. Keep the
                // first id, then park any id past the table at 0.
                uint32_t flags = Rd32(dst + 0x4);
                uint16_t texId = Rd16(dst + kParticleTextureIdOff);
                if (flags & kParticleFlagMultiTex) texId &= kParticleTextureIdMask;
                if (md->textures.count && texId >= md->textures.count) texId = 0;
                Wr16(dst + kParticleTextureIdOff, texId);

                // blendingType @+0x28: the Client blend table is stride-7 (modes 0..6), so Source BlendAdd
                // (7) indexes out of its row. BlendAdd = (ONE, INV_SRC_ALPHA) = the engine's mode 3, so 7
                // maps to 3 exactly; any other unknown mode > 7 falls back to Add (4).
                uint8_t* blend = dst + kParticleBlendOff;
                if (*blend == 7)     *blend = 3;
                else if (*blend > 7) *blend = 4;

                // Flipbook: wrap the head/tail cell keys into [0, rows*cols). The Source wraps the sampled
                // cell by the atlas cell count; the Client does not, so a key >= cols samples off the atlas.
                uint16_t rows = Rd16(dst + kParticleTexRowsOff);
                uint16_t cols = Rd16(dst + kParticleTexColsOff);
                int16_t  cells = static_cast<int16_t>(rows * cols);
                if (cells > 1)
                {
                    const uint32_t cellTracks[2] = { kParticleHeadCellOff, kParticleTailCellOff };
                    for (uint32_t track : cellTracks)
                    {
                        uint32_t keyCount = Rd32(dst + track + 0x8);
                        uint32_t keyOfs   = Rd32(dst + track + 0xc);
                        if (keyCount == 0 || keyCount > 0x1000 || keyOfs == 0) continue;
                        if (static_cast<size_t>(keyOfs) + keyCount * 2 > fileSize) continue;
                        auto* keys = reinterpret_cast<int16_t*>(md->base() + keyOfs);
                        for (uint32_t k = 0; k < keyCount; ++k)
                            if (keys[k] >= cells) keys[k] %= cells;
                    }
                }

                // Compressed gravity (flags 0x800000): the gravity track keys are packed
                // {int8 x, int8 y, int16 z}, not floats. Decompress each to the plain Client float scalar
                // (+downward) and clear the flag; otherwise the 4 packed bytes read as a float can form a
                // NaN and poison the particle position. Two-level track: outer {count,ofs} at +0x90/+0x94
                // (model-relative), one inner array per animation index. Only EMBEDDED animations carry
                // their keys in this model (sequence flag 0x20 set); an external animation's keys live in a
                // separate file, so adding this model's base would corrupt unrelated bytes - skip them.
                if (flags & kParticleFlagCompressedGravity)
                {
                    uint32_t outerCount = Rd32(dst + kParticleGravityValCountOff);
                    uint32_t outerOfs   = Rd32(dst + kParticleGravityValOfsOff);
                    const uint8_t* seqs = (md->sequences.count && md->sequences.offset)
                                        ? md->base() + md->sequences.offset : nullptr;
                    if (outerOfs && outerCount && outerCount <= 0x1000 &&
                        static_cast<size_t>(outerOfs) + outerCount * 8 <= fileSize)
                    {
                        uint8_t* outer = md->base() + outerOfs;
                        for (uint32_t o = 0; o < outerCount; ++o)
                        {
                            if (seqs && o < md->sequences.count &&
                                (Rd32(seqs + o * 0x40 + 0x0c) & 0x20) == 0)
                                continue;
                            uint32_t innerCount = Rd32(outer + o * 8 + 0x0);
                            uint32_t innerOfs   = Rd32(outer + o * 8 + 0x4);
                            if (!innerOfs || !innerCount || innerCount > 0x1000) continue;
                            if (static_cast<size_t>(innerOfs) + innerCount * 4 > fileSize) continue;
                            uint8_t* keys = md->base() + innerOfs;
                            for (uint32_t k = 0; k < innerCount; ++k)
                            {
                                uint8_t* key = keys + k * 4;
                                float dx = static_cast<int8_t>(key[0]) / 128.0f;
                                float dy = static_cast<int8_t>(key[1]) / 128.0f;
                                int16_t zraw; std::memcpy(&zraw, key + 2, 2);
                                float planar = dx * dx + dy * dy;
                                float zc  = std::sqrt(planar < 1.0f ? 1.0f - planar : 0.0f);
                                float mag = zraw * kParticleGravityMagUnit;
                                if (mag < 0.0f) { zc = -zc; mag = -mag; }
                                float scalar = -(zc * mag);
                                std::memcpy(key, &scalar, 4);
                            }
                        }
                    }
                    Wr32(dst + 0x4, flags & ~kParticleFlagCompressedGravity);
                }
            }
        }

        // Slide each Source ribbon emitter onto the Client stride and clamp its texture/material indices
        // into the header tables. The Source tail lands in the Client layout's padding, so the strides are
        // equal and the slide is a no-op while strideSource == strideClient; it is written against the
        // named strides so a future version that grows the body only edits the Source value. The
        // textureIndices/materialIndices arrays are still file-relative (count,offset) pairs; the slide
        // leaves them at +0x14/+0x1c so the native de-relocator's pointer-fix reads them correctly.
        void CompactRibbons(M2Header* md, uint32_t fileSize)
        {
            if (!md->ribbonEmitters.count || !md->ribbonEmitters.offset) return;
            static_assert(kRibbonStrideSource >= kRibbonStrideClient,
                          "ribbon forward slide requires dst stride <= src stride");

            uint8_t* arr = md->base() + md->ribbonEmitters.offset;
            for (uint32_t i = 0; i < md->ribbonEmitters.count; ++i)
            {
                uint8_t* src = arr + i * kRibbonStrideSource;
                uint8_t* dst = arr + i * kRibbonStrideClient;
                if (dst != src) std::memmove(dst, src, kRibbonStrideClient);

                auto* rb = reinterpret_cast<M2Ribbon*>(dst);
                if (md->textures.count && rb->textureIndices.offset &&
                    static_cast<size_t>(rb->textureIndices.offset) + rb->textureIndices.count * 2 <= fileSize)
                {
                    auto* texIdx = reinterpret_cast<uint16_t*>(md->base() + rb->textureIndices.offset);
                    for (uint32_t t = 0; t < rb->textureIndices.count; ++t)
                        if (texIdx[t] >= md->textures.count) texIdx[t] = 0;
                }
                if (md->materials.count && rb->materialIndices.offset &&
                    static_cast<size_t>(rb->materialIndices.offset) + rb->materialIndices.count * 2 <= fileSize)
                {
                    auto* matIdx = reinterpret_cast<uint16_t*>(md->base() + rb->materialIndices.offset);
                    for (uint32_t m = 0; m < rb->materialIndices.count; ++m)
                        if (matIdx[m] >= md->materials.count) matIdx[m] = 0;
                }
            }
        }

        // Index of the first sequence whose id equals anim, or -1.
        int16_t AnimationIndex(const M2Sequence* seqs, uint32_t count, uint16_t anim)
        {
            for (uint32_t i = 0; i < count; ++i)
                if (seqs[i].id == anim) return static_cast<int16_t>(i);
            return -1;
        }

        // Point sequenceLookup[newId] at newPos if it still holds oldPos, else rewrite the first lookup
        // entry that holds oldPos. A -1 oldPos (AnimationIndex miss) matches nothing and is a no-op.
        void ReplaceAnimLookup(int16_t* lookup, uint32_t lookupCount, int16_t oldPos, uint16_t newId,
                               int16_t newPos)
        {
            if (newId < lookupCount && lookup[newId] == oldPos) { lookup[newId] = newPos; return; }
            for (uint32_t i = 0; i < lookupCount; ++i)
                if (lookup[i] == oldPos) { lookup[i] = newPos; break; }
        }

        // Two transforms over the file-relative sequence array:
        //  - EVERY sequence: mask blendTime to its low u16. The Source split that u32 into in|out; read
        //    whole by the Client it is a huge blend duration so transitions never complete.
        //  - id above the Client max: remap a curated swim/jump set to a Client id and patch the lookup so
        //    the engine still resolves them.
        void FixAnimations(M2Header* md)
        {
            if (!md->sequences.count || !md->sequences.offset) return;
            auto* seqs = reinterpret_cast<M2Sequence*>(md->base() + md->sequences.offset);
            uint32_t count = md->sequences.count;

            int16_t* lookup = nullptr;
            uint32_t lookupCount = 0;
            if (md->sequenceLookup.count && md->sequenceLookup.offset)
            {
                lookup      = reinterpret_cast<int16_t*>(md->base() + md->sequenceLookup.offset);
                lookupCount = md->sequenceLookup.count;
            }

            for (uint32_t i = 0; i < count; ++i)
            {
                uint16_t id = seqs[i].id;
                if (id > kClientMaxAnimId)
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

        // Container chunk magic carrying the texture FileDataID table (one u32 per header.textures entry).
        constexpr uint32_t kChunkTXID = 0x44495854; // 'TXID'

        // Walk the container's IFF chunks and copy out the TXID payload as a FileDataID array. Each chunk is
        // {magic, u32 size, size bytes}; the MD21 chunk's body is skipped over by its size, so an inner-body
        // dword that happens to read as 'TXID' is never mistaken for a chunk. Empty if there is no TXID.
        std::vector<uint32_t> ScanTxid(std::span<const uint8_t> in)
        {
            std::vector<uint32_t> ids;
            size_t i = 0;
            while (i + 8 <= in.size())
            {
                uint32_t magic = Rd32(in.data() + i);
                uint32_t size  = Rd32(in.data() + i + 4);
                if (static_cast<size_t>(i) + 8 + size > in.size())
                    break;
                if (magic == kChunkTXID)
                {
                    ids.resize(size / 4);
                    for (uint32_t k = 0; k < size / 4; ++k)
                        ids[k] = Rd32(in.data() + i + 8 + k * 4);
                    break;
                }
                i += 8 + size;
            }
            return ids;
        }

        // Re-inline TXID texture FileDataIDs as native filename strings. Modern type-0 textures carry an
        // empty filename and reference the file by FileDataID in the container TXID chunk; the native loader
        // reads the inline filename, so each empty type-0 slot is resolved to a path appended to the model
        // tail and its filename M2Array is pointed at it. Existing inline names and runtime-replaced slots
        // (type != 0) are left untouched. Returns true if any name was inlined.
        bool ReinlineTextures(std::vector<uint8_t>& model, const std::vector<uint32_t>& txid,
                              const ResolveCtx& rc)
        {
            if (txid.empty() || !rc.resolve)
                return false;

            auto* md = reinterpret_cast<M2Header*>(model.data());
            const uint32_t texCount = md->textures.count;
            const uint32_t texOff   = md->textures.offset;
            if (!texCount || !texOff ||
                static_cast<size_t>(texOff) + static_cast<size_t>(texCount) * sizeof(M2Texture) > model.size())
                return false;

            bool inlined = false;
            for (uint32_t i = 0; i < texCount; ++i)
            {
                // Re-read through the buffer base each step: an append below can reallocate `model`.
                auto* tex = reinterpret_cast<M2Texture*>(model.data() + texOff + i * sizeof(M2Texture));
                if (tex->type != kTexTypeHardcoded)
                    continue; // runtime-replaced slot: no file
                if (tex->filename.count > 1 && tex->filename.offset &&
                    static_cast<size_t>(tex->filename.offset) < model.size())
                    continue; // already has an inline name
                if (i >= txid.size() || txid[i] == 0)
                    continue;

                std::string path;
                if (!rc.resolve(rc.user, txid[i], path) || path.empty())
                    continue;

                const uint32_t newOff = static_cast<uint32_t>(model.size());
                model.insert(model.end(), path.begin(), path.end());
                model.push_back(0);

                tex = reinterpret_cast<M2Texture*>(model.data() + texOff + i * sizeof(M2Texture));
                tex->filename.count  = static_cast<uint32_t>(path.size()) + 1;
                tex->filename.offset = newOff;
                inlined = true;
            }
            return inlined;
        }
    }

    bool TranslateM2(std::span<const uint8_t> in, const ResolveCtx& rc, std::vector<uint8_t>& out)
    {
        if (in.size() < sizeof(ContainerHeader)) return false;

        const uint32_t magic = Rd32(in.data());
        const bool isContainer = magic == kMagicMD21;

        // Already a target-shaped self-contained model: nothing the Source path rewrites.
        if (!isContainer)
        {
            if (magic != kMagicMD20 || in.size() < sizeof(M2Header)) return false;
            const uint32_t version = Rd32(in.data() + 4);
            if (version < kSourceVersionMin || version > kSourceVersionMax)
                return false; // target version or unknown: serve raw
        }

        out.assign(in.begin(), in.end());

        uint32_t innerSize = isContainer ? DeChunk(out) : static_cast<uint32_t>(out.size());
        if (innerSize == 0 && isContainer) { out.clear(); return false; } // malformed container

        if (out.size() < sizeof(M2Header)) { out.clear(); return false; }
        auto* md = reinterpret_cast<M2Header*>(out.data());
        if (md->magic != kMagicMD20 ||
            md->version < kSourceVersionMin || md->version > kSourceVersionMax)
        {
            out.clear();
            return false;
        }

        const uint32_t fileSize = static_cast<uint32_t>(out.size());

        // TXID lives in the container chunks dropped by DeChunk, so read it from the original input.
        std::vector<uint32_t> txid;
        if (isContainer)
            txid = ScanTxid(in);

        CompactCameras(md);
        CompactParticles(md, fileSize);
        CompactRibbons(md, fileSize);
        FixAnimations(md);

        // Re-inline the externalized texture paths last: it appends to `out` and may reallocate, so it must
        // run after every transform that holds an M2Header pointer into the buffer.
        ReinlineTextures(out, txid, rc);

        return true;
    }
}
