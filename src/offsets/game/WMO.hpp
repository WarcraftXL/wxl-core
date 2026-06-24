// Map-object engine entries (root/group load, material resolve, visibility) and runtime object fields.
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

// INTERNAL to the core. Map-object engine entries (root/group load, material resolve, visibility) and
// runtime object fields. Modules never include this; they use wxl::game / wxl::events.
namespace wxl::offsets::game::wmo
{
    // --- load (rewrite buffers before the native parse) ---
    // Root read-completion callback (root): fires once after the async read fills the root buffer and
    // before the chunk walker runs.
    constexpr uintptr_t kRootComplete = 0x007D8050;
    // Group reader (group): the join point of both the sync and async group-load paths, run before the
    // group sub-chunk walk.
    constexpr uintptr_t kGroupParse = 0x007D82E0;

    // --- instance placement (per-instance scale) ---
    // Spawns a placed WMO instance from one MODF record and builds its world transform. __cdecl
    // (ctx, modf, tileOrigin, dedup); arg2 is the 0x40-byte MODF record, the return is the instance.
    // When dedup is set and the uniqueId is already loaded it returns the existing instance instead.
    constexpr uintptr_t kSpawnFromModf = 0x007BF460;
    using Wmo_SpawnFromModfFn = void*(__cdecl*)(void* ctx, void* modf, const float* tileOrigin, int dedup);
    // MODF record: u16 per-instance scale at +0x3E (factor = value/1024; 0 and 1024 both mean 1.0). The
    // Client treats it as padding and renders every WMO at 1.0.
    constexpr size_t kOffModfScale = 0x3E;
    // Instance transform matrices (4x4 row-major floats): +0x70 the render rotation basis, +0xB0 the
    // collision/portal copy. A fresh instance's basis is orthonormal; a uniform 3x3-row scale of both
    // resizes the rendered and the collided WMO together.
    constexpr size_t kOffInstanceRenderMatrix    = 0x70;
    constexpr size_t kOffInstanceCollisionMatrix = 0xB0;

    // --- material / visibility guards ---
    // Material/texture resolver (model, materialIndex): computes the material entry and resolves its
    // texture-name offsets. It does not bounds-check materialIndex.
    constexpr uintptr_t kResolveMaterialTexture = 0x007D7710;
    // Portal-visibility traversal (model, groupIndex, a, b, out): portal-driven group visibility. It
    // assumes every referenced group object exists.
    constexpr uintptr_t kPortalVisibility = 0x007AF520;
    // Group-resident accessor (model, groupIndex, force): the join point of the resident-group queries.
    // It does not bounds-check groupIndex.
    constexpr uintptr_t kGroupResidentAccessor = 0x007AEA80;

    // --- root object fields ---
    constexpr size_t kOffRootBuffer    = 0x1CC; // root buffer pointer
    constexpr size_t kOffRootSize      = 0x1D0; // root buffer byte size
    constexpr size_t kOffNameInline    = 0x1C;  // inline path string
    constexpr size_t kOffMaterialBase  = 0x160; // material-record base pointer
    constexpr size_t kOffMaterialCount = 0x19C; // material count
    constexpr size_t kOffGroupArray    = 0x1F8; // group runtime-object array (stride 4)
    constexpr size_t kOffGroupCount    = 0x1F4; // group count (the group-array bound)
    // Per-group bbox table on the root.
    constexpr size_t kOffMogiTable     = 0x130; // group-info table base pointer
    constexpr size_t kMogiStride       = 0x20;  // group-info entry stride
    constexpr size_t kOffMogiBbox      = 0x04;  // bbox min within an entry (max at +0x10)

    // --- group object fields ---
    constexpr size_t kOffGroupBuffer = 0x184; // group buffer pointer
    constexpr size_t kOffGroupSize   = 0x188; // group buffer byte size
    constexpr size_t kOffGroupRoot   = 0x18C; // -> parent root object

    // --- visibility-probe entries and globals (cull path) ---
    constexpr uintptr_t kPortalRectAccum   = 0x007A8F20; // (portal, moprRef, portalState, exteriorFlag)
    constexpr uintptr_t kFrustumAabbTest   = 0x009839E0; // (frustum, bbox); 0=culled, 3=inside
    constexpr uintptr_t kHorizonAabbTest   = 0x0078FDC0; // (bbox, mode); 0=visible, 2=horizon-culled
    constexpr uintptr_t kCameraInGroupTest = 0x007AE880; // (root, camA, camB, groupIndex)
    constexpr uintptr_t kIndoorFlag        = 0x00CD87A4; // != 0 when camera is in an indoor group
    // Same global as kIndoorFlag, read as a pointer: the map-object instance the camera is currently
    // inside (null when outdoors). Its kOffInstanceRoot field points to the root that carries the path.
    constexpr uintptr_t kCurrentInteriorInstance = 0x00CD87A4;
    // Instance field: pointer to the owning root object (the one with the inline path at kOffNameInline).
    constexpr size_t kOffInstanceRoot = 0xF4;
    constexpr uintptr_t kPortalRect        = 0x00ADF58C; // float[5]: minX,minY,maxX,maxY,nearExtent
    constexpr uintptr_t kOutdoorEnabled    = 0x00ADF59C; // float; >= 0 when the outdoor pass runs

    // --- camera-in-group containment (cull path) ---
    constexpr uintptr_t kBspRaycastRefine = 0x007CB0C0;
    // Group collision fields (group object). MOVI = u16[3] indices per face; MOVT = C3Vector vertices.
    constexpr size_t kOffGroupBsp       = 0x64;  // CAaBsp container (null when the group has no BSP)
    constexpr size_t kOffGroupMovi      = 0xE0;  // triangle vertex indices (u16[3] per face, stride 6)
    constexpr size_t kOffGroupMovt      = 0xE8;  // vertices (C3Vector, stride 0x0C)
    constexpr size_t kOffGroupMoviCount = 0x154; // MOVI face count
    // CAaBsp fields (relative to kOffGroupBsp).
    constexpr size_t kOffBspNodes     = 0x00; // node array pointer (0 when the group has no BSP)
    constexpr size_t kOffBspMobr      = 0x08; // collision face-index array (u16 into MOVI)
    constexpr size_t kOffBspMobrCount = 0x10; // collision face-index count
    constexpr size_t kOffBspBboxMax   = 0x58; // local bbox max (3 floats); min at +0x4C

    // --- signatures ---
    // Root read-completion (root on stack).
    using Wmo_RootCompleteFn = void(__cdecl*)(void* root);
    // Group reader: native this-in-ECX.
    using WmoGroup_ParseFn = void(__fastcall*)(void* group, void* edx);
    // Material/texture resolver: native this-in-ECX; declared with a dummy second parameter so the
    // trampoline keeps materialIndex on the stack.
    using Wmo_ResolveMaterialTextureFn = void(__fastcall*)(void* model, void* edx, int materialIndex);
    // Portal-visibility traversal: native this-in-ECX; declared with a dummy second parameter so the
    // trampoline keeps the trailing arguments on the stack.
    using Wmo_PortalVisibilityFn = unsigned int(__fastcall*)(void* model, void* edx, unsigned int groupIndex, float* a, float* b, unsigned int* out);
    // Group-resident accessor: native this-in-ECX; declared with a dummy second parameter so the
    // trampoline keeps the trailing arguments on the stack.
    using Wmo_GroupResidentFn = unsigned int(__fastcall*)(void* model, void* edx, unsigned int groupIndex, unsigned int force);
    // Visibility-probe signatures.
    using Wmo_PortalRectAccumFn = void(__fastcall*)(void* portal, void* edx, void* moprRef, void* portalState, int exteriorFlag);
    using Wmo_FrustumAabbTestFn = uint32_t(__fastcall*)(void* frustum, void* edx, void* bbox);
    using Wmo_HorizonAabbTestFn = uint32_t(__cdecl*)(void* bbox, uint32_t mode);
    using Wmo_CameraInGroupTestFn = uint32_t(__fastcall*)(void* root, void* edx, float* camA, float* camB, uint32_t groupIndex);
    using Wmo_BspRaycastRefineFn = char(__fastcall*)(void* group, void* edx, float* seg, float* distScale, unsigned int mask, void* a3, void* a4, void* instance);

    // --- typed views over the objects above ---
    // The constants are the curated landmarks; these structs give named, typed access to the same fields,
    // with every member offset checked against a constant at compile time (a wrong padding fails the build).
    // Only RE'd fields are named; the gaps are explicit padding. Pointers are 4 bytes on the 32-bit client.
#pragma pack(push, 1)
    /** @brief Map-object root: the parsed root object that owns the material table and the group array. */
    struct Root
    {
        uint8_t  _pad00[kOffNameInline];
        char     nameInline[kOffMogiTable - kOffNameInline]; // kOffNameInline (inline NUL-terminated path)
        void*    mogiTable;        // kOffMogiTable -> per-group bbox table (stride kMogiStride)
        uint8_t  _pad134[kOffMaterialBase - (kOffMogiTable + sizeof(void*))];
        void*    materialBase;     // kOffMaterialBase -> material-record base
        uint8_t  _pad164[kOffMaterialCount - (kOffMaterialBase + sizeof(void*))];
        uint32_t materialCount;    // kOffMaterialCount
        uint8_t  _pad1a0[kOffRootBuffer - (kOffMaterialCount + sizeof(uint32_t))];
        void*    rootBuffer;       // kOffRootBuffer -> root file buffer
        uint32_t rootSize;         // kOffRootSize (root buffer byte size)
        uint8_t  _pad1d4[kOffGroupCount - (kOffRootSize + sizeof(uint32_t))];
        uint32_t groupCount;       // kOffGroupCount (the group-array bound)
        void*    groupArray[1];    // kOffGroupArray (group runtime objects, stride 4)
    };
    static_assert(offsetof(Root, nameInline)    == kOffNameInline,    "Root.nameInline");
    static_assert(offsetof(Root, mogiTable)     == kOffMogiTable,     "Root.mogiTable");
    static_assert(offsetof(Root, materialBase)  == kOffMaterialBase,  "Root.materialBase");
    static_assert(offsetof(Root, materialCount) == kOffMaterialCount, "Root.materialCount");
    static_assert(offsetof(Root, rootBuffer)    == kOffRootBuffer,    "Root.rootBuffer");
    static_assert(offsetof(Root, rootSize)      == kOffRootSize,      "Root.rootSize");
    static_assert(offsetof(Root, groupCount)    == kOffGroupCount,    "Root.groupCount");
    static_assert(offsetof(Root, groupArray)    == kOffGroupArray,    "Root.groupArray");

    /** @brief Map-object group: a runtime group object holding its file buffer and a back pointer to the root. */
    struct Group
    {
        uint8_t  _pad00[kOffGroupBuffer];
        void*    groupBuffer;      // kOffGroupBuffer -> group file buffer
        uint32_t groupSize;        // kOffGroupSize (group buffer byte size)
        void*    root;             // kOffGroupRoot -> parent root object
    };
    static_assert(offsetof(Group, groupBuffer) == kOffGroupBuffer, "Group.groupBuffer");
    static_assert(offsetof(Group, groupSize)   == kOffGroupSize,   "Group.groupSize");
    static_assert(offsetof(Group, root)        == kOffGroupRoot,   "Group.root");

    /** @brief Group-info entry (root->mogiTable + i * kMogiStride): the per-group world AABB. */
    struct MogiEntry
    {
        uint8_t  _pad00[kOffMogiBbox];
        float    bboxMin[3];       // kOffMogiBbox (bbox min)
        float    bboxMax[3];       // bbox max (+0x10 within an entry)
    };
    static_assert(offsetof(MogiEntry, bboxMin)     == kOffMogiBbox,       "MogiEntry.bboxMin");
    static_assert(offsetof(MogiEntry, bboxMax)     == kOffMogiBbox + 0xC, "MogiEntry.bboxMax");
    static_assert(offsetof(MogiEntry, bboxMin) + 0xC == kOffMogiBbox + 0x10 - sizeof(float), "MogiEntry.bbox.layout");
    static_assert(sizeof(MogiEntry) <= kMogiStride, "MogiEntry fits stride");
#pragma pack(pop)
}
