// M2 target contract: the model record layout the client's native loader consumes.
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

#include <cstddef>
#include <cstdint>

/**
 * @brief The model record layout the client's native M2 loader reads.
 *
 * The native loader reads one model shape: these records. They are the common target every M2
 * down-port compacts onto and know nothing about any source build. A support module declares how
 * its source records differ (larger strides, extra fields) and slides them onto these structs
 * before the parse.
 */
namespace wxl::structure::m2
{
    constexpr uint32_t kMagicMD20 = 0x3032444D; // 'MD20' self-contained model (the only magic the loader accepts)
    constexpr uint32_t kMagicMD21 = 0x3132444D; // 'MD21' chunked container (a source wrapper; de-chunked elsewhere)

    constexpr uint32_t kClientVersion = 264;    // the loader's native inner version; every source compacts to it

    constexpr uint32_t kFlagUseTextureCombinerCombos = 0x8; // global_flags: header carries that trailing array

#pragma pack(push, 1)

    /** 
     * @brief A count plus offset pair referencing an array in the model buffer.
     */
    struct M2Array { uint32_t count; uint32_t offset; };

    /**
     * @brief The MD20 header: the exact on-disk layout the loader reads.
     */
    struct M2Header
    {
        uint32_t magic;                  // 0x00
        uint32_t version;                // 0x04
        M2Array  name;                   // 0x08
        uint32_t globalFlags;            // 0x10
        M2Array  globalLoops;            // 0x14
        M2Array  sequences;              // 0x1C
        M2Array  sequenceLookup;         // 0x24
        M2Array  bones;                  // 0x2C
        M2Array  boneLookup;             // 0x34
        M2Array  vertices;               // 0x3C
        uint32_t numSkinProfiles;        // 0x44
        M2Array  colors;                 // 0x48
        M2Array  textures;               // 0x50
        M2Array  textureWeights;         // 0x58
        M2Array  textureTransforms;      // 0x60
        M2Array  textureReplacements;    // 0x68
        M2Array  materials;              // 0x70
        M2Array  boneCombos;             // 0x78
        M2Array  textureCombos;          // 0x80
        M2Array  textureUnitLookup;      // 0x88
        M2Array  textureWeightCombos;    // 0x90
        M2Array  textureTransformCombos; // 0x98
        float    boundingBox[6];         // 0xA0
        float    boundingSphereRadius;   // 0xB8
        float    collisionBox[6];        // 0xBC
        float    collisionSphereRadius;  // 0xD4
        M2Array  collisionIndices;       // 0xD8
        M2Array  collisionPositions;     // 0xE0
        M2Array  collisionNormals;       // 0xE8
        M2Array  attachments;            // 0xF0
        M2Array  attachmentLookup;       // 0xF8
        M2Array  events;                 // 0x100
        M2Array  lights;                 // 0x108
        M2Array  cameras;                // 0x110
        M2Array  cameraLookup;           // 0x118
        M2Array  ribbonEmitters;         // 0x120
        M2Array  particleEmitters;       // 0x128
        M2Array  textureCombinerCombos;  // 0x130  (only if globalFlags & kFlagUseTextureCombinerCombos)

        /** 
         * @brief Returns the header base as a byte pointer.
         */
        uint8_t*       base()       { return reinterpret_cast<uint8_t*>(this); }

        /** 
         * @brief Returns the header base as a const byte pointer.
         */
        const uint8_t* base() const { return reinterpret_cast<const uint8_t*>(this); }
    };

    /**
     * @brief One animation sequence, 0x40 bytes.
     *
     * The loader reads a single u32 duration at 0x04 and a single u32 blendTime at 0x1C. flags bit
     * 0x20 = data embedded in the model; clear = streamed from a sequence file.
     */
    struct M2Sequence
    {
        uint16_t id;             // 0x00
        uint16_t variationIndex; // 0x02
        uint32_t duration;       // 0x04
        float    movespeed;      // 0x08
        uint32_t flags;          // 0x0C  bit 0x20 = embedded
        int16_t  frequency;      // 0x10
        uint16_t _pad12;         // 0x12
        uint32_t replayMin;      // 0x14
        uint32_t replayMax;      // 0x18
        uint32_t blendTime;      // 0x1C
        uint8_t  _bounds[0x1C];  // 0x20
        int16_t  variationNext;  // 0x3C
        uint16_t aliasNext;      // 0x3E
    };

    /**
     * @brief One texture, 0x10 bytes.
     *
     * type 0 = hardcoded: filename holds a NUL-terminated path (count includes the NUL).
     */
    struct M2Texture
    {
        uint32_t type;     // 0x00
        uint32_t flags;    // 0x04
        M2Array  filename; // 0x08
    };

    // Common texture.type values used by the 3.3.5 client.
    constexpr uint32_t kTexTypeHardcoded = 0;
    constexpr uint32_t kTexTypeObjectSkin = 2;
    constexpr uint32_t kTexTypeWeaponBlade = 3;

    /**
     * @brief One render batch (texunit), 0x18 bytes.
     *
     * shaderId selects the program; materialIndex indexes header.materials (render flags plus
     * blend); textureCoordComboIndex indexes header.textureUnitLookup.
     */
    struct M2Batch
    {
        uint8_t  flags;                      // 0x00
        uint8_t  priorityPlane;              // 0x01
        uint16_t shaderId;                   // 0x02
        uint16_t skinSectionIndex;           // 0x04
        uint16_t geosetIndex;                // 0x06
        uint16_t colorIndex;                 // 0x08
        uint16_t materialIndex;              // 0x0A  index into header.materials
        uint16_t materialLayer;             // 0x0C
        uint16_t textureCount;               // 0x0E
        uint16_t textureComboIndex;          // 0x10
        uint16_t textureCoordComboIndex;     // 0x12  index into header.textureUnitLookup
        uint16_t textureWeightComboIndex;    // 0x14
        uint16_t textureTransformComboIndex; // 0x16
    };

    /**
     * @brief One skin submesh, 0x30 bytes.
     *
     * level > 0 = a (level<<16 | id) sub-batch the loader does not handle.
     */
    struct M2SkinSection
    {
        uint16_t skinSectionId;     // 0x00
        uint16_t level;             // 0x02
        uint16_t vertexStart;       // 0x04
        uint16_t vertexCount;       // 0x06
        uint16_t indexStart;        // 0x08
        uint16_t indexCount;        // 0x0A
        uint16_t boneCount;         // 0x0C
        uint16_t boneComboIndex;    // 0x0E
        uint16_t boneInfluences;    // 0x10
        uint16_t centerBoneIndex;   // 0x12
        float    centerPosition[3]; // 0x14
        float    sortCenterPos[3];  // 0x20
        float    sortRadius;        // 0x2C
    };

    /**
     * @brief One ribbon emitter, 0xb0 bytes.
     *
     * textureIndices and materialIndices index header.textures and header.materials.
     */
    struct M2Ribbon
    {
        uint32_t ribbonId;        // 0x00
        uint32_t boneIndex;       // 0x04
        float    position[3];     // 0x08
        M2Array  textureIndices;  // 0x14  into header.textures
        M2Array  materialIndices; // 0x1C  into header.materials
        uint8_t  _tracks[0x8C];   // 0x24  color/alpha/height/texSlot/visibility tracks + scalars
    };

    /** 
     * @brief Shared camera track body (position/target/roll tracks plus bases).
     */
    struct M2CameraBody { uint8_t trackData[0x54]; };

    /** 
     * @brief One camera, carrying an explicit fov float at 0x04.
     */
    struct M2Camera
    {
        uint32_t     type;     // 0x00
        float        fov;      // 0x04
        float        farClip;  // 0x08
        float        nearClip; // 0x0C
        M2CameraBody body;     // 0x10
    };

#pragma pack(pop)

    static_assert(sizeof(M2Array) == 8, "M2Array");
    static_assert(offsetof(M2Header, vertices) == 0x3C, "vertices");
    static_assert(offsetof(M2Header, cameras) == 0x110, "cameras");
    static_assert(offsetof(M2Header, particleEmitters) == 0x128, "particleEmitters");
    static_assert(offsetof(M2Header, textureCombinerCombos) == 0x130, "textureCombinerCombos");
    static_assert(sizeof(M2Sequence) == 0x40, "M2Sequence");
    static_assert(offsetof(M2Sequence, blendTime) == 0x1C, "blendTime");
    static_assert(sizeof(M2Texture) == 0x10, "M2Texture");
    static_assert(sizeof(M2Batch) == 0x18, "M2Batch");
    static_assert(offsetof(M2Batch, shaderId) == 0x02, "M2Batch.shaderId");
    static_assert(offsetof(M2Batch, materialIndex) == 0x0A, "M2Batch.materialIndex");
    static_assert(offsetof(M2Batch, textureCount) == 0x0E, "M2Batch.textureCount");
    static_assert(sizeof(M2SkinSection) == 0x30, "M2SkinSection");
    static_assert(offsetof(M2SkinSection, level) == 0x02, "M2SkinSection.level");
    static_assert(sizeof(M2Ribbon) == 0xB0, "M2Ribbon");
    static_assert(sizeof(M2Camera) == 0x64, "M2Camera");
}
