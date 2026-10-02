// Map-doodad (placed M2) runtime object fields and the per-chunk doodad list layout.
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

#include <cstdint>
#include <cstddef>

// INTERNAL to the core. The runtime placed-doodad object (one per map M2 placement) and the per-chunk
// list that holds them. The placement transform lives here, not on the shared render model. The chunk
// lookup itself is in offsets/game/ADT.hpp (kGetChunk). Modules never include this; they use wxl::game.
namespace wxl::offsets::game::doodad
{
    // --- spawn ---
    // Builds a placed-doodad object from an MDDF placement (modelName, MDDF entry, tile origin).
    // Returns the new object pointer in EAX. The "a placed doodad was created" point.
    constexpr uintptr_t kSpawnFromMDDF = 0x007BECD0;
    // __cdecl, 3 stack args, returns the new placed-doodad object pointer.
    using SpawnFromMDDFFn = void*(__cdecl*)(const char* modelName, void* mddf, void* tileOrigin);

    // The MDDF record kSpawnFromMDDF reads (0x24 bytes, standard MDDF layout, unchanged since
    // Classic): nameId/uniqueId (u32 each) at +0x00/+0x04, position (3 floats, archived coordinate
    // convention) at +0x08/+0x0C/+0x10, rotation at +0x14/+0x18/+0x1C, scale (u16, 1024 = 100%) at
    // +0x20, flags (u16) at +0x22 -- the last two already load-bearing elsewhere in this codebase.
    constexpr size_t kMddfPosX  = 0x08;
    constexpr size_t kMddfPosY  = 0x0C;
    constexpr size_t kMddfPosZ  = 0x10;
    // Uniform scale, u16 with 1024 = 100%. The spawn path's own conversion, byte-verified at the call
    // site that fills kScale: doodad.scale = (float)mddf.scaleU16 * kMddfScaleToFloat. Reading it here
    // is what lets a caller size a placement BEFORE the model exists to be measured.
    constexpr size_t kMddfScale = 0x20;
    constexpr float  kMddfScaleToFloat = 0.0009765625f; // 1/1024
    // World position from an MDDF record, confirmed at kSpawnFromMDDF's own call site:
    // worldX = tileOrigin.x - mddf.posZ, worldY = tileOrigin.y - mddf.posX,
    // worldZ = tileOrigin.z + mddf.posY -- the same axis-swap/negate every archived-coordinate
    // consumer in this codebase applies. tileOrigin is the constant {17066.666, 17066.666, 0} for
    // every MDDF-driven spawn (the per-chunk placement walk never passes anything else), so a caller
    // that only cares about ordinary terrain placements can use that literal instead of threading the
    // pointer through.
    constexpr float kMddfTileOriginX = 17066.666f;
    constexpr float kMddfTileOriginY = 17066.666f;

    // Runtime teardown of one resident placed doodad -- releases its render context (clearing the
    // event/sequence callbacks first, matching kSetEventCallback/kSetSequenceCallback's own wiring at
    // spawn, see offsets/game/M2.hpp), unlinks it from its owning chunk's list and its own global
    // list, and returns it to the doodad pool. __cdecl, 1 stack arg (the doodad). No-op (does not
    // free) when the doodad's own refcount (+0xA) is still nonzero -- a caller must have already
    // dropped every link referencing it first, which this codebase does not yet do directly; only
    // observe this hook, never call it to force a premature free.
    constexpr uintptr_t kDoodadPurge = 0x007C3020;
    using DoodadPurgeFn = void(__cdecl*)(void* doodad);

    // --- placed-doodad object fields ---
    constexpr size_t kFlags = 0x0C; // 1 = normal placement

    // World position (C3Vector).
    constexpr size_t kPosX  = 0x6C;
    constexpr size_t kPosY  = 0x70;
    constexpr size_t kPosZ  = 0x74;
    constexpr size_t kScale = 0x78; // uniform scale

    // bbox min / sphere center / bbox max. WARNING: at spawn all three are set equal to the position (a
    // degenerate point), never the model's real extents and never recomputed. Not usable as a real box;
    // a real wireframe must transform the model's local bounds by the live instance matrix.
    constexpr size_t kBBoxMinX = 0x38;
    constexpr size_t kBBoxMinY = 0x3C;
    constexpr size_t kBBoxMinZ = 0x40;
    constexpr size_t kCenterX  = 0x48;
    constexpr size_t kCenterY  = 0x4C;
    constexpr size_t kCenterZ  = 0x50;
    constexpr size_t kBBoxMaxX = 0x54;
    constexpr size_t kBBoxMaxY = 0x58;
    constexpr size_t kBBoxMaxZ = 0x5C;

    // Staging matrix (float[16], row-major) composed once at spawn. NOT what the renderer reads: the spawn
    // copies it once into the model render instance (kInstWorldMatrix), then never touches it again.
    // Writing here does NOT move the model; kept only as the editor-facing source of truth.
    constexpr size_t kWorldMatrix      = 0xD8;
    constexpr size_t kWorldMatrixTransX = 0x108;
    constexpr size_t kWorldMatrixTransY = 0x10C;
    constexpr size_t kWorldMatrixTransZ = 0x110;

    // --- model render instance (the object the renderer actually draws) ---
    // doodad+0x34 -> model render instance. The live world matrix the renderer multiplies every frame
    // lives on the instance, not the doodad. To move/rotate/scale a placed doodad you write kInstWorldMatrix.
    constexpr size_t kInstance        = 0x34; // doodad -> model render instance (0 mid async-load)
    constexpr size_t kInstWorldMatrix = 0xB4; // float[16] row-major, READ every frame, never rewritten
    constexpr size_t kInstTransX      = 0xE4; // translation row of the live matrix (= world X/Y/Z)
    constexpr size_t kInstTransY      = 0xE8;
    constexpr size_t kInstTransZ      = 0xEC;
    constexpr size_t kInstModel       = 0x2C; // instance -> model cache node

    // --- model cache node (holds the file path + the parsed MD20 header) ---
    constexpr size_t kModelFullPath = 0x3C;  // inline NUL-terminated normalized path (take address)
    constexpr size_t kModelFileName = 0x140; // char* to the bare filename (points into the +0x3C buffer)
    constexpr size_t kModelHeader   = 0x150; // ptr to the parsed MD20 header blob (the local-bounds source)

    // --- MD20 header (H = *(model+0x150)): model-LOCAL bounding box ---
    // Transform these by the instance matrix (kInstWorldMatrix) to get the real world box of a placement.
    constexpr size_t kHdrBBoxMinX = 0xA0; // C3Vector local AABB min
    constexpr size_t kHdrBBoxMinY = 0xA4;
    constexpr size_t kHdrBBoxMinZ = 0xA8;
    constexpr size_t kHdrBBoxMaxX = 0xAC; // C3Vector local AABB max
    constexpr size_t kHdrBBoxMaxY = 0xB0;
    constexpr size_t kHdrBBoxMaxZ = 0xB4;

    // --- per-chunk doodad list ---
    constexpr size_t kChunkDoodadLinkOff = 0xC4;
    constexpr size_t kChunkDoodadHead    = 0xCC;
    constexpr size_t kNodeDoodad         = 0x04;

    // --- typed views over the objects above ---
    // The constants are the curated landmarks; these structs give named, typed access to the same fields,
    // with every member offset checked against a constant at compile time (a wrong padding fails the build).
    // Only known fields are named; the gaps are explicit padding. Pointers are 4 bytes on the 32-bit client.
#pragma pack(push, 1)
    /** @brief Placed-doodad object: one per map M2 placement (the "d" pointer). */
    struct MapDoodad
    {
        uint8_t  _pad00[kFlags];
        uint32_t flags;            // kFlags
        uint8_t  _pad10[kInstance - (kFlags + sizeof(uint32_t))];
        void*    instance;         // kInstance -> M2Instance
        float    bboxMin[3];       // kBBoxMin (degenerate at spawn)
        uint8_t  _pad44[kCenterX - (kBBoxMinX + 3 * sizeof(float))];
        float    center[3];        // kCenter
        float    bboxMax[3];       // kBBoxMax
        uint8_t  _pad60[kPosX - (kBBoxMaxX + 3 * sizeof(float))];
        float    pos[3];           // kPos
        float    scale;            // kScale
        uint8_t  _pad7c[kWorldMatrix - (kScale + sizeof(float))];
        float    worldMatrix[16];  // kWorldMatrix (staging copy; not what the renderer reads)
    };
    static_assert(offsetof(MapDoodad, flags)       == kFlags,       "MapDoodad.flags");
    static_assert(offsetof(MapDoodad, instance)    == kInstance,    "MapDoodad.instance");
    static_assert(offsetof(MapDoodad, bboxMin)     == kBBoxMinX,    "MapDoodad.bboxMin");
    static_assert(offsetof(MapDoodad, center)      == kCenterX,     "MapDoodad.center");
    static_assert(offsetof(MapDoodad, bboxMax)     == kBBoxMaxX,    "MapDoodad.bboxMax");
    static_assert(offsetof(MapDoodad, pos)         == kPosX,        "MapDoodad.pos");
    static_assert(offsetof(MapDoodad, scale)       == kScale,       "MapDoodad.scale");
    static_assert(offsetof(MapDoodad, worldMatrix) == kWorldMatrix, "MapDoodad.worldMatrix");
    static_assert(offsetof(MapDoodad, worldMatrix) + 12 * sizeof(float) == kWorldMatrixTransX, "MapDoodad.worldMatrix.trans");

    /** @brief CM2 render instance (doodad+0x34): holds the live world matrix the renderer reads each frame. */
    struct M2Instance
    {
        uint8_t  _pad00[kInstModel];
        void*    model;            // kInstModel -> M2ModelCache
        uint8_t  _pad30[kInstWorldMatrix - (kInstModel + sizeof(void*))];
        float    worldMatrix[16];  // kInstWorldMatrix (READ every frame)
    };
    static_assert(offsetof(M2Instance, model)       == kInstModel,       "M2Instance.model");
    static_assert(offsetof(M2Instance, worldMatrix) == kInstWorldMatrix, "M2Instance.worldMatrix");
    static_assert(offsetof(M2Instance, worldMatrix) + 12 * sizeof(float) == kInstTransX, "M2Instance.worldMatrix.trans");

    /** @brief Model cache node (instance+0x2c): inline file path plus the parsed MD20 header pointer. */
    struct M2ModelCache
    {
        uint8_t  _pad00[kModelFullPath];
        char     fullPath[kModelFileName - kModelFullPath]; // kModelFullPath (inline NUL-terminated path)
        char*    fileName;         // kModelFileName (points into fullPath)
        uint8_t  _pad144[kModelHeader - (kModelFileName + sizeof(char*))];
        void*    header;           // kModelHeader -> MD20Header
    };
    static_assert(offsetof(M2ModelCache, fullPath) == kModelFullPath, "M2ModelCache.fullPath");
    static_assert(offsetof(M2ModelCache, fileName) == kModelFileName, "M2ModelCache.fileName");
    static_assert(offsetof(M2ModelCache, header)   == kModelHeader,   "M2ModelCache.header");

    /** @brief MD20 header (modelCache->header): the model-LOCAL bounding box. */
    struct MD20Header
    {
        uint8_t  _pad00[kHdrBBoxMinX];
        float    bboxMin[3];       // kHdrBBoxMin
        float    bboxMax[3];       // kHdrBBoxMax
    };
    static_assert(offsetof(MD20Header, bboxMin) == kHdrBBoxMinX, "MD20Header.bboxMin");
    static_assert(offsetof(MD20Header, bboxMax) == kHdrBBoxMaxX, "MD20Header.bboxMax");
#pragma pack(pop)
}
