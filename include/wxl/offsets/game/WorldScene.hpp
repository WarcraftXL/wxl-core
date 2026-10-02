// Indoor/outdoor viewer-group resolution globals (camera-driven group locate).
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

// INTERNAL to the core. The engine resolves which WMO group(s) the render CAMERA currently sits in,
// every frame, purely from camera position (never the character's).
namespace wxl::offsets::game::worldscene
{
    // Primary resolved viewer group (0 = camera outside any WMO group; otherwise the group the camera's
    // vertical containment ray resolved into). Read by the scene render to pick the outdoor / indoor
    // branch.
    constexpr uintptr_t kViewerGroupId = 0x00CD87A4;
    // Second-candidate flag: true when a second, overlapping candidate group was also found this frame
    // (a boolean, not a counter -- the resolver's output arrays hold at most 2 slots).
    constexpr uintptr_t kViewerSecondGroupFlag = 0x00CD87A0;

    // __fastcall(groupEntry): a one-line wrapper stamping a WMO group's bbox-diagonal into the shared
    // 384-bucket horizon/skyline occlusion buffer, so a later clip-buffer cull test may reject a smaller
    // object behind it. The stamp is a crude 2-point (bbox min/max corner) approximation with no
    // object-identity or size-class guard: any group's stamp can occlude any other tested afterward,
    // regardless of relative size -- on a dense cluster of similarly-sized small props this spuriously
    // occludes neighbors that should both be visible.
    constexpr uintptr_t kGroupClipStamp = 0x007CC880;
    using GroupClipStampFn = void(__fastcall*)(void* groupEntry);
    // The group-entry's own world-space AABB min/max corners sit inline at its head (3 floats each).
    constexpr size_t kOffGroupBboxMin = 0x00;
    constexpr size_t kOffGroupBboxMax = 0x0C;

    // __cdecl(C3Vector* points, int count, uint8 mode). Generic open-polyline stamper into the shared
    // clip buffer: stamps the count-1 consecutive (i, i+1) edges, no wraparound. Reused directly to
    // stamp a group's real silhouette instead of its bbox diagonal.
    constexpr uintptr_t kClipBufferStampPolyline = 0x0078F900;
    using ClipBufferStampPolylineFn = void(__cdecl*)(const float* points, int count, uint8_t mode);

    // groupEntry -> owning map-object instance ("defInstance") two-level link. groupEntry+0x20 is a
    // possibly-tagged/possibly-null pointer to a small link node; that node's +0x8 holds defInstance. The
    // engine itself treats this as fallible (LSB-tagged sentinel = "not linked"); any consumer must
    // replicate that check and skip (never dereference blindly).
    constexpr size_t kOffGroupLinkPtr  = 0x20;
    constexpr size_t kOffGroupIndex    = 0x50; // int, index into the shared model's group table
    constexpr size_t kOffLinkDefInst   = 0x08; // linkNode + 0x8 = defInstance (map-object instance*)

    // __thiscall(this=sharedModel, groupIndex, forceFlag) -> group runtime object or null. this is
    // *(defInstance + wmo::kOffInstanceRoot); forceFlag=1 returns the group object even if its own
    // "loaded" flag isn't set yet.
    constexpr uintptr_t kGetGroup = 0x007AEA80;
    using GetGroupFn = void*(__thiscall*)(void* sharedModel, int groupIndex, int forceFlag);

    // __thiscall(this=groupObject, raySegmentLocal[2], inOutMaxFraction, 0, 0, scratchByte, param7) ->
    // bool. rayLocal is in the WMO instance's LOCAL space (transform via kMulVecMatrix and
    // wmo::kOffInstanceCollisionMatrix first). Returns true AND inOutMaxFraction shrunk below its input
    // value when the ray hit real geometry within that fraction.
    constexpr uintptr_t kGetTris = 0x007CB0C0;
    using GetTrisFn = bool(__thiscall*)(void* groupObject, const float raySegmentLocal[6],
                                         float* inOutMaxFraction, int param4, int param5,
                                         uint8_t* scratchByte, int param7);

    // __cdecl(C3Vector* out, const C3Vector* v, const float m[16]) -> out = v * m (row-vector
    // convention). The engine's own world<->local transform helper, reused as-is rather than
    // reimplemented.
    constexpr uintptr_t kMulVecMatrix = 0x004C21B0;
    using MulVecMatrixFn = void(__cdecl*)(float out[3], const float v[3], const float m[16]);

    // __thiscall(this=defInstance, groupEntry, frustumCorners, flag). Its ONE caller (the cell+0x14 list
    // walk) already resolves and validates defInstance (via groupEntry+0x20 -> +0x8) before making this
    // call. The original clip-buffer stamp (kGroupClipStamp above) is reached via a different, unrelated
    // list (cell+0x44) whose groupEntry+0x20 is not guaranteed valid, so resolving defInstance from there
    // directly is unsafe. This function is the correct hook point for logic needing a proven-valid
    // defInstance/groupEntry pair: detour as a call-through (call original first, preserving its own
    // frustum-cull/exterior-stamp/portal-walk work), then use the two pointers this hook already
    // receives as arguments -- zero new dereferences of groupEntry+0x20 needed.
    constexpr uintptr_t kCullMapObjDefGroupFromExterior = 0x007B3A10;
    using CullMapObjDefGroupFromExteriorFn = void(__fastcall*)(void* defInstance, void* edx, void* groupEntry,
                                                                 float* frustumCorners, int flag);

    // World scene and view setup
    /// Object fade-out distance in one call - the lever an extension needs to push doodad/object
    /// popping out with a raised far clip. __cdecl, caller-cleaned.
    constexpr uintptr_t kFadeDistanceScale                 = 0x0078F570;
    /// The per-frame decision of which tile/group the viewer occupies, which drives interior vs
    /// exterior and the whole cull path - the hook for correcting viewer placement in modern WMOs.
    /// __cdecl, caller-cleaned.
    constexpr uintptr_t kViewerLocate                      = 0x00795D40;
    /// Releases the scene's default textures and sort tables - matching teardown for Scene.Initialize.
    /// __cdecl, caller-cleaned.
    constexpr uintptr_t kDestroy                           = 0x00798310;
    /// Where the scene's default textures and cull parameters are seeded - the place to substitute
    /// defaults once instead of per frame. __cdecl, caller-cleaned.
    constexpr uintptr_t kInitialize                        = 0x007997D0;
    /// The whole world scene draw in one frame - the outermost place to wrap terrain+WMO+doodad
    /// rendering with extension render state, above every per-pass hook already recorded. __cdecl,
    /// caller-cleaned.
    constexpr uintptr_t kRender                            = 0x0079A870;
}
