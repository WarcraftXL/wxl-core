// Generic M3-family intermediate model: what any container reader (HotS/SC2/WoW MD34) parses
// into, and what the client-model bake (M2Build) consumes. Carries no reader-specific byte layout.
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

#include "../../common/Math.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace wxl::modern::assets::m3
{
    using namespace wxl::modern::assets::common;

    struct Vertex
    {
        Vec3    pos;
        uint8_t w[4];   // bone weights, source order
        uint8_t bl[4];  // region-relative bone lookup indices
        Vec3    normal;
        float   uv0[2];
        float   uv1[2];
    };

    struct Region
    {
        uint32_t firstVert, numVerts, firstFace, numFaces;
        uint16_t numBones, firstLookup;
    };

    struct Batch
    {
        uint16_t region;
        uint32_t matIndex; // standard-material index (0 when the source type is not standard)
        float    alpha = 1.0f; // batch opacity, drives a color entry when below 1
    };

    struct Material
    {
        uint32_t    blend;   // source blend mode
        std::string diffuse; // diffuse image path, else emissive fallback, else empty
        uint32_t    uvAnim = 0;         // uv-offset anim id of the picked layer
        float       uvRate[2] = { 0, 0 }; // uv scroll per millisecond, resolved by the host
        bool        sheetUv = false;    // used by a ribbon sheet: U runs along the length
    };

    struct BoneRec
    {
        std::string name;
        int16_t     parent;
        uint32_t    locId, rotId, scaleId; // track anim ids
        Vec3        initLoc;
        Quat        initRot;
        Vec3        initScale;
    };

    struct Emitter
    {
        uint32_t bone;
        uint32_t mat;       // standard-material index
        std::string image;  // particle atlas image; FX materials carry it outside the diffuse slot
        float    speed[2];  // min, max
        float    life[2];   // min, max
        float    angle[2];  // emission axis tilt around bone-local X/Y, degrees
        float    spread[2]; // emission cone half-angles, radians
        float    zacc;
        float    mid[3];    // size / color / alpha midpoint times (fraction of life)
        float    sizes[3];  // start / middle / final size
        uint8_t  colors[3][4]; // start / middle / final RGBA
        float    rate;
        uint32_t rateAnimId = 0xFFFFFFFF; // resolves against the anim library when not null:
                                           // some emitters fire in bursts keyed to gait, not
                                           // a constant stream (e.g. footstep dust)
        float    areaSize[3];
        float    areaRadius;
        uint16_t rows, cols;   // flipbook atlas
        uint8_t  cellFirst, cellLast;
    };

    struct Ribbon
    {
        uint32_t bone;
        uint32_t mat;       // standard-material index
        std::string image;  // trail texture, resolved like the emitter atlases
        float width;        // ribbon size
        float length;       // trail duration, seconds
    };

    /**
     * @brief Format-agnostic parsed model: what every M3-family container reader (HotS/SC2/WoW
     *        MD34, one reader per family member) populates, and the only thing M2Build reads.
     */
    struct Model
    {
        std::vector<Vertex>   verts;
        std::vector<uint16_t> vertRegion; // owning region per vertex
        std::vector<uint16_t> faces;      // region-relative indices
        std::vector<Region>   regions;
        std::vector<Batch>    batches;
        std::vector<Material> materials;
        std::vector<BoneRec>  bones;
        std::vector<Mat4>     irefs;      // inverse rest matrices, model-frame adjusted
        std::vector<uint16_t> lookup;     // bone lookup table
        std::vector<Emitter>  particles;
        std::vector<Ribbon>   ribbons;
        Vec3                  bmin{}, bmax{};
        float                 radius = 0;
    };

    /**
     * @brief Converts the model's ribbons into textured strip meshes skinned to their bones.
     *
     * Source sheet-style ribbons (wings, standing banners) are permanent shapes, not motion
     * trails; the client's ribbons only grow from bone movement, so those become geometry.
     * The sheet fades toward its tip through length bands of decreasing batch alpha.
     *
     * Pure geometry op on the parsed Model: no container byte layout involved, so it applies
     * unchanged regardless of which reader produced the model.
     * @param out              model to rewrite; its ribbon list is consumed
     * @param tiltDeg          sheet elongation angle: 0 = along the bone's local Z, 90 = local X
     * @param lenScale         sheet length multiplier; texel density is preserved
     * @param baseWidthScale   base width relative to the tip; tip width is always unscaled.
     *                         interpolated per-vertex so the silhouette stays smooth
     *                         (1.0 = uniform width, current default)
     */
    void RibbonsToMesh(Model& out, float tiltDeg, float lenScale, float baseWidthScale);

    // --- animation library: also format-agnostic once populated by a reader ---

    struct LibBone
    {
        uint32_t loc, rot, scale; // track anim ids
        Vec3     initLoc;
        Quat     initRot;
        Vec3     initScale;
    };

    struct LibSeq
    {
        std::string           name;
        uint32_t              start, end, flags;
        std::vector<uint32_t> stcs;
    };

    struct LibStc
    {
        std::unordered_map<uint32_t, uint32_t> ids; // animId -> (sdSlot << 16) | index
        uint32_t sdOffset[13];                      // file offset of each SD array
        uint32_t sdCount[13];
        uint16_t conc;                              // runsConcurrent (overlay layer)
    };

    /** @brief A resolved keyframe track inside the library file. */
    struct LibTrack
    {
        const int32_t* times = nullptr;
        const uint8_t* values = nullptr;
        uint32_t       count = 0;
        uint32_t       width = 0;
    };

    struct AnimLib
    {
        std::vector<uint8_t>                     data; // owned file bytes (tracks point into it)
        std::unordered_map<std::string, LibBone> bones;
        std::vector<LibSeq>                      seqs;
        std::vector<LibStc>                      stcs;
        // Byte stride of one SD (sequence-data) holder entry, stamped by the reader that
        // populated sdOffset/sdCount above -- the SD holder layout is stable across the M3
        // family today, but Track() reads this field rather than a hardcoded constant so a
        // future reader can override it if that ever stops being true.
        uint32_t sdStride = 32;

        /**
         * @brief Resolves an anim id in one collection to its keyframe track.
         * @param stc     collection index
         * @param animId  the animation reference id
         * @param slot    expected SD slot (2 = vec3, 3 = quat)
         * @param width   key stride in bytes
         * @param out     receives the track view
         * @return true when the id resolves to a track in that slot
         */
        bool Track(uint32_t stc, uint32_t animId, uint32_t slot, uint32_t width, LibTrack& out) const;
    };
}
