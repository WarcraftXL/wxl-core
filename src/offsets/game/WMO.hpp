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
    constexpr uintptr_t kPortalRect        = 0x00ADF58C; // float[5]: minX,minY,maxX,maxY,nearExtent
    constexpr uintptr_t kOutdoorEnabled    = 0x00ADF59C; // float; >= 0 when the outdoor pass runs

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
}
