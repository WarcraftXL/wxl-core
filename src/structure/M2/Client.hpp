// Client contract: the target model layout the native loader consumes.
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

#pragma once

#include <cstddef>
#include <cstdint>

// HOST-only. The TARGET model contract: the exact on-disk MD20 layout the native loader reads.
// The native parser expects ONE shape (the classic MD20, inner version kVersion). A modern model is
// made to satisfy this shape on the host before the client parses it: the chunked container is
// de-chunked and the modern record layouts are compacted onto these structs. The Source contract
// declares the modern superset (Source.hpp).
namespace wraith::structure::m2
{
    // MD20 self-contained model magic; the native parser accepts only this.
    constexpr uint32_t kMagicMD20 = 0x3032444D; // 'MD20'

    // Target inner version. The Source range compacts down to this contract.
    constexpr uint32_t kVersion = 264;

    // global_flags bit: header carries a textureCombinerCombos array.
    constexpr uint32_t kFlagUseTextureCombinerCombos = 0x8;

#pragma pack(push, 1)

    // An (count, offset) pair. In a file the offset is MD20-relative; the loader rewrites it to a raw
    // pointer in place once parsed.
    struct M2Array
    {
        uint32_t count;
        uint32_t offset;
    };

    // The MD20 header: the exact on-disk layout the native parser reads.
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
        M2Array  materials;              // 0x70  (render flags + blend mode)
        M2Array  boneCombos;             // 0x78
        M2Array  textureCombos;          // 0x80  (texunit -> texture index)
        M2Array  textureUnitLookup;      // 0x88  (texCoordCombos: per-texture UV set, -1 = env)
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

        uint8_t*       base()       { return reinterpret_cast<uint8_t*>(this); }
        const uint8_t* base() const { return reinterpret_cast<const uint8_t*>(this); }
    };

    // One animation sequence, 0x40 bytes. The native loader reads a single duration at 0x04, flags at
    // 0x0C, the replay (min,max) range at 0x14/0x18, variationNext/aliasNext at 0x3C/0x3E, and takes the
    // play time modulo duration. blendTime at 0x1C is a single u32 here; the Source splits it into two
    // u16, so the dword is masked to its low u16 to restore this contract. flags bit 0x20 = data embedded
    // in the model; clear = streamed from an external sequence file.
    struct M2Sequence
    {
        uint16_t id;             // 0x00  animation id
        uint16_t variationIndex; // 0x02
        uint32_t duration;       // 0x04  milliseconds (play time is taken modulo this)
        float    movespeed;      // 0x08
        uint32_t flags;          // 0x0C  bit 0x20 = embedded; clear = external sequence file
        int16_t  frequency;      // 0x10
        uint16_t _pad12;         // 0x12
        uint32_t replayMin;      // 0x14
        uint32_t replayMax;      // 0x18
        uint32_t blendTime;      // 0x1C  target: u32; Source: blendTimeIn(u16)|blendTimeOut(u16)
        uint8_t  _bounds[0x1C];  // 0x20  bounds (box + radius)
        int16_t  variationNext;  // 0x3C
        uint16_t aliasNext;      // 0x3E
    };

    // One texture definition, 0x10 bytes. type 0 = hardcoded: the filename M2Array holds a NUL-terminated
    // path (count includes the NUL). type != 0 = a runtime-replaced slot (skin/hair/...), no file. Modern
    // models externalize the type-0 path into the container TXID chunk and leave filename empty; the host
    // re-inlines it so the native name-based loader resolves the texture.
    struct M2Texture
    {
        uint32_t type;     // 0x00  0 = hardcoded filename; else runtime-replaced slot
        uint32_t flags;    // 0x04  0x1 = wrap x, 0x2 = wrap y
        M2Array  filename; // 0x08  NUL-terminated path (count includes the NUL)
    };

    // texture.type 0 = hardcoded: the only type whose pixels come from a named file.
    constexpr uint32_t kTexTypeHardcoded = 0;

    // One render batch (texunit), 0x18 bytes.
    struct M2Batch
    {
        uint8_t  flags;                     // 0x00
        uint8_t  priorityPlane;             // 0x01
        uint16_t shaderId;                  // 0x02
        uint16_t skinSectionIndex;          // 0x04
        uint16_t geosetIndex;               // 0x06
        uint16_t colorIndex;                // 0x08
        uint16_t materialIndex;             // 0x0A  (index into M2Header.materials = render flags)
        uint16_t materialLayer;             // 0x0C
        uint16_t textureCount;              // 0x0E
        uint16_t textureComboIndex;         // 0x10
        uint16_t textureCoordComboIndex;    // 0x12
        uint16_t textureWeightComboIndex;   // 0x14
        uint16_t textureTransformComboIndex;// 0x16
    };

    // One ribbon emitter, 0xb0 bytes. textureIndices/materialIndices index header.textures/materials.
    // The Source appends three trailing fields into this layout's tail padding (0xac..0xb0), so the
    // stride is unchanged across versions.
    struct M2Ribbon
    {
        uint32_t ribbonId;            // 0x00
        uint32_t boneIndex;           // 0x04
        float    position[3];         // 0x08
        M2Array  textureIndices;      // 0x14  into header.textures
        M2Array  materialIndices;     // 0x1c  into header.materials
        uint8_t  colorTrack[0x14];    // 0x24
        uint8_t  alphaTrack[0x14];    // 0x38
        uint8_t  heightAboveTrack[0x14]; // 0x4c
        uint8_t  heightBelowTrack[0x14]; // 0x60
        float    edgesPerSecond;      // 0x74
        float    edgeLifetime;        // 0x78
        float    gravity;             // 0x7c
        uint16_t textureRows;         // 0x80
        uint16_t textureCols;         // 0x82
        uint8_t  texSlotTrack[0x14];  // 0x84
        uint8_t  visibilityTrack[0x14]; // 0x98
        int16_t  priorityPlane;       // 0xac  Source (tail padding)
        int8_t   ribbonColorIndex;    // 0xae  Source
        int8_t   textureTransformLookupIndex; // 0xaf  Source
    };

    // One submesh, 0x30 bytes. level > 0 = a (level<<16 | id) sub-batch the target engine does not handle.
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

    // Shared camera track body (position/target/roll tracks + their bases). Identical across versions.
    struct M2CameraBody { uint8_t trackData[0x54]; };

    // The target camera carries an explicit fov float at 0x04.
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
    static_assert(offsetof(M2Header, version) == 0x04, "version");
    static_assert(offsetof(M2Header, globalFlags) == 0x10, "globalFlags");
    static_assert(offsetof(M2Header, vertices) == 0x3C, "vertices");
    static_assert(offsetof(M2Header, textures) == 0x50, "textures");
    static_assert(offsetof(M2Header, materials) == 0x70, "materials");
    static_assert(offsetof(M2Header, textureCombos) == 0x80, "textureCombos");
    static_assert(offsetof(M2Header, textureUnitLookup) == 0x88, "textureUnitLookup");
    static_assert(offsetof(M2Header, cameras) == 0x110, "cameras");
    static_assert(offsetof(M2Header, particleEmitters) == 0x128, "particleEmitters");
    static_assert(offsetof(M2Header, textureCombinerCombos) == 0x130, "textureCombinerCombos");
    static_assert(sizeof(M2Texture) == 0x10, "M2Texture");
    static_assert(offsetof(M2Texture, filename) == 0x08, "M2Texture.filename");
    static_assert(sizeof(M2Sequence) == 0x40, "M2Sequence");
    static_assert(offsetof(M2Sequence, duration) == 0x04, "duration");
    static_assert(offsetof(M2Sequence, flags) == 0x0C, "flags");
    static_assert(offsetof(M2Sequence, blendTime) == 0x1C, "blendTime");
    static_assert(offsetof(M2Sequence, variationNext) == 0x3C, "variationNext");
    static_assert(sizeof(M2Batch) == 0x18, "M2Batch");
    static_assert(offsetof(M2Batch, shaderId) == 0x02, "shaderId");
    static_assert(offsetof(M2Batch, materialIndex) == 0x0A, "materialIndex");
    static_assert(offsetof(M2Batch, textureCount) == 0x0E, "textureCount");
    static_assert(sizeof(M2Ribbon) == 0xB0, "M2Ribbon");
    static_assert(offsetof(M2Ribbon, textureIndices) == 0x14, "M2Ribbon.textureIndices");
    static_assert(offsetof(M2Ribbon, materialIndices) == 0x1C, "M2Ribbon.materialIndices");
    static_assert(offsetof(M2Ribbon, priorityPlane) == 0xAC, "M2Ribbon.priorityPlane");
    static_assert(sizeof(M2SkinSection) == 0x30, "M2SkinSection");
    static_assert(offsetof(M2SkinSection, level) == 0x02, "level");
    static_assert(sizeof(M2Camera) == 0x64, "M2Camera");
}
