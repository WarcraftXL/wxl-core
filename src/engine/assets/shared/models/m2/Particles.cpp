// Compacts the source particle emitter and scopes the source alpha-key cutoff at draw time.
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

#include "Particles.hpp"

#include "Contract.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>

#ifndef WXL_HOST
#include "game/m2/M2.hpp" // PushAlphaRef: live-engine, DLL-only draw path
#endif

namespace wxl::modern::assets::m2::particles
{
    namespace fmt = wxl::structure::m2;

    namespace
    {
        // Emitter strides: the source form is 16 bytes larger than the client (inner version > 271, or
        // global_flags bit 0x200). The native de-relocator strides by the client value, so source emitters
        // are slid down to the client stride before the parse.
        constexpr uint32_t kStrideClient = 0x1DC;
        constexpr uint32_t kStrideSource = 0x1EC;

        // textureId @+0x16: the client reads a flat u16 index into header.textures. The multi-texture form
        // (flag 0x10000) packs three 5-bit ids, so keep the first and park the rest.
        constexpr uint32_t kTextureIdOff  = 0x16;
        constexpr uint32_t kFlagMultiTex  = 0x10000;
        constexpr uint16_t kTextureIdMask = 0x1F;

        // blendingType @+0x28: the client blend table is stride-7 (modes 0..6); source BlendAdd (7) maps to
        // mode 3 exactly, any other mode > 7 falls back to Add (4).
        constexpr uint32_t kBlendOff = 0x28;

        // Flipbook atlas: rows@+0x30 / cols@+0x32 subdivide the texture; head/tail cell tracks at +0x13c /
        // +0x14c (keys = int16 cell indices at block+0x8 count / +0xc ofs). The source wraps the cell, the
        // client does not, so a cell >= cols samples off the atlas; wrap the keys.
        constexpr uint32_t kTexRowsOff  = 0x30;
        constexpr uint32_t kTexColsOff  = 0x32;
        constexpr uint32_t kHeadCellOff = 0x13C;
        constexpr uint32_t kTailCellOff = 0x14C;

        // Compressed gravity (flag 0x800000): the gravity track keys are packed {int8 x, int8 y, int16 z}
        // direction+magnitude, not floats. The value array {count,ofs} is at +0x90/+0x94 (two-level: outer
        // by animation, inner = the 4-byte keys). Decompress to a plain client float and clear the flag.
        constexpr uint32_t kFlagCompressedGravity = 0x800000;
        constexpr uint32_t kGravityValCountOff    = 0x90;
        constexpr uint32_t kGravityValOfsOff      = 0x94;
        constexpr float    kGravityMagUnit        = 0.04238648f; // yards per key-unit

        // The lowered alpha-key cutoff for source content: the coverage midpoint where source leaf / foliage
        // coverage-alpha sits. blend mode 1 = alpha key (the only mode the lowered reference applies to).
        constexpr float kSourceAlphaKeyRef = 0.5f;
    }

    /**
     * @brief Compacts every source emitter onto the client stride in place and normalizes its
     *        source-only encodings.
     *
     * dst stride < src stride, so the forward walk never overwrites an emitter before it is read.
     * @param md        Model header (pre-parse, offsets model-relative).
     * @param fileSize  Total model size, bounding the sub-array reads.
     */
    void Compact(fmt::M2Header* md, uint32_t fileSize)
    {
        if (!md->particleEmitters.count || !md->particleEmitters.offset) return;
        uint8_t* arr = md->base() + md->particleEmitters.offset;
        for (uint32_t i = 0; i < md->particleEmitters.count; ++i)
        {
            uint8_t* s = arr + i * kStrideSource;
            uint8_t* d = arr + i * kStrideClient;
            if (d != s) std::memmove(d, s, kStrideClient);

            // A multi-texture emitter packs three 5-bit ids into textureId; the client reads it flat into
            // header.textures, so the packed value overruns the handle table. Keep the first id; park any
            // id past the table at 0.
            uint32_t flags = Rd32(d + 0x4);
            uint16_t texId = Rd16(d + kTextureIdOff);
            if (flags & kFlagMultiTex) texId &= kTextureIdMask;
            if (md->textures.count && texId >= md->textures.count) texId = 0;
            Wr16(d + kTextureIdOff, texId);

            // blendingType @+0x28: source BlendAdd (7) indexes out of the client's stride-7 table. It is the
            // engine's mode 3 exactly; any other unknown mode > 7 falls back to Add (4).
            uint8_t* blend = d + kBlendOff;
            if (*blend == 7)     *blend = 3;
            else if (*blend > 7) *blend = 4;

            // Flipbook: wrap the head/tail cell keys into [0, rows*cols). The source wraps the sampled cell
            // by the atlas cell count; the client does not, so a key >= cols samples off the atlas.
            uint16_t rows = Rd16(d + kTexRowsOff);
            uint16_t cols = Rd16(d + kTexColsOff);
            int16_t  cells = static_cast<int16_t>(rows * cols);
            if (cells > 1)
            {
                const uint32_t cellTracks[2] = { kHeadCellOff, kTailCellOff };
                for (uint32_t track : cellTracks)
                {
                    uint32_t keyCount = Rd32(d + track + 0x8);
                    uint32_t keyOfs   = Rd32(d + track + 0xc);
                    if (keyCount == 0 || keyCount > 0x1000 || keyOfs == 0) continue;
                    if (static_cast<size_t>(keyOfs) + keyCount * 2 > fileSize) continue;
                    auto* keys = reinterpret_cast<int16_t*>(md->base() + keyOfs);
                    for (uint32_t k = 0; k < keyCount; ++k)
                        if (keys[k] >= cells) keys[k] %= cells;
                }
            }

            // Compressed gravity (flags 0x800000): the gravity track keys are packed {int8 x, int8 y,
            // int16 z}, not floats. Decompress each to the plain client float scalar (+downward) and clear
            // the flag; otherwise the 4 packed bytes read as a float can form a NaN and poison the particle
            // position. Two-level track: outer {count,ofs} at +0x90/+0x94, one inner array per animation
            // index. Only EMBEDDED animations (sequence flag 0x20) carry their keys in this model; an
            // external animation's keys live in a separate file, so skip them.
            if (flags & kFlagCompressedGravity)
            {
                uint32_t outerCount = Rd32(d + kGravityValCountOff);
                uint32_t outerOfs   = Rd32(d + kGravityValOfsOff);
                const uint8_t* seqs = (md->sequences.count && md->sequences.offset)
                                    ? md->base() + md->sequences.offset : nullptr;
                if (outerOfs && outerCount && outerCount <= 0x1000 &&
                    static_cast<size_t>(outerOfs) + outerCount * 8 <= fileSize)
                {
                    uint8_t* outer = md->base() + outerOfs;
                    for (uint32_t o = 0; o < outerCount; ++o)
                    {
                        if (seqs && o < md->sequences.count &&
                            (Rd32(seqs + o * sizeof(fmt::M2Sequence) + 0x0c) & 0x20) == 0)
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
                            float mag = zraw * kGravityMagUnit;
                            if (mag < 0.0f) { zc = -zc; mag = -mag; }
                            float scalar = -(zc * mag);
                            std::memcpy(key, &scalar, 4);
                        }
                    }
                }
                Wr32(d + 0x4, flags & ~kFlagCompressedGravity);
            }
        }
    }

#ifndef WXL_HOST
    /**
     * @brief Lowers the alpha-key cutoff to the source coverage midpoint for an alpha-key batch of a
     *        downported model.
     * @param a           Batch alpha/material setup arguments.
     * @param downported  True if the model was reshaped by this module.
     */
    void OnSetupBatchAlpha(const wxl::events::M2SetupBatchAlphaArgs& a, bool downported)
    {
        if (downported && a.blendMode == kBlendAlphaKey)
            wxl::game::m2::PushAlphaRef(kSourceAlphaKeyRef);
    }
#endif
}
