// CWorldScene indoor/outdoor viewer-group resolution globals (camera-driven group locate).
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

// INTERNAL to the core. CWorldScene::LocateViewer3/CMap::LocateViewerMapObjs resolve which WMO group(s)
// the render CAMERA currently sits in, every frame, purely from camera position (never the character's).
namespace wxl::offsets::game::worldscene
{
    // Primary resolved viewer group (0 = camera outside any WMO group; otherwise the group the camera's
    // vertical containment ray resolved into). Read by CWorldScene::Render to pick the outdoor / indoor
    // branch.
    constexpr uintptr_t kViewerGroupId = 0x00CD87A4;
    // Second-candidate flag: true when a second, overlapping candidate group was also found this frame
    // (a boolean, not a counter -- CMap::LocateViewerMapObjs' output arrays hold at most 2 slots).
    constexpr uintptr_t kViewerSecondGroupFlag = 0x00CD87A0;

    // FUN_007cc880: __fastcall(groupEntry), a one-line wrapper stamping a WMO group's bbox-diagonal into
    // the shared 384-bucket horizon/skyline occlusion buffer (CWorldScene::clipBuffer, 0x00cd8938) so a
    // later CWorldScene::ClipBufferCull test (0x0079a221 / 0x007b3a76) may reject a smaller object behind
    // it. The stamp is a crude 2-point (bbox min/max corner) approximation with no object-identity or
    // size-class guard: any group's stamp can occlude any other tested afterward, regardless of relative
    // size -- on a dense cluster of similarly-sized small props this spuriously occludes neighbors that
    // should both be visible.
    constexpr uintptr_t kGroupClipStamp = 0x007CC880;
    using GroupClipStampFn = void(__fastcall*)(void* groupEntry);
    // The group-entry's own world-space AABB min/max corners sit inline at its head (3 floats each).
    constexpr size_t kOffGroupBboxMin = 0x00;
    constexpr size_t kOffGroupBboxMax = 0x0C;

    // FUN_0078f900: __cdecl(C3Vector* points, int count, uint8 mode). Generic open-polyline stamper into
    // the shared clip buffer: stamps the count-1 consecutive (i, i+1) edges, no wraparound. Reused
    // directly to stamp a group's real silhouette instead of its bbox diagonal (see
    // wmo_portal_camera_cull.md section 19/22).
    constexpr uintptr_t kClipBufferStampPolyline = 0x0078F900;
    using ClipBufferStampPolylineFn = void(__cdecl*)(const float* points, int count, uint8_t mode);

    // groupEntry -> owning CMapObjDef ("defInstance") two-level link (section 22.2). groupEntry+0x20 is a
    // possibly-tagged/possibly-null pointer to a small link node; that node's +0x8 holds defInstance. The
    // native engine itself treats this as fallible (LSB-tagged sentinel = "not linked"); any consumer must
    // replicate that check and skip (never dereference blindly).
    constexpr size_t kOffGroupLinkPtr  = 0x20;
    constexpr size_t kOffGroupIndex    = 0x50; // int, index into the shared model's group table
    constexpr size_t kOffLinkDefInst   = 0x08; // linkNode + 0x8 = defInstance (CMapObjDef*)

    // CMapObj__GetGroup: __thiscall(this=sharedModel, groupIndex, forceFlag) -> group runtime object or
    // null. this is *(defInstance + wmo::kOffInstanceRoot); forceFlag=1 returns the group object even if
    // its own "loaded" flag isn't set yet (section 22.1).
    constexpr uintptr_t kGetGroup = 0x007AEA80;
    using GetGroupFn = void*(__thiscall*)(void* sharedModel, int groupIndex, int forceFlag);

    // CMapObjGroup__GetTris (the 0x007cb0c0 overload): __thiscall(this=groupObject, raySegmentLocal[2],
    // inOutMaxFraction, 0, 0, scratchByte, param7) -> bool. rayLocal is in the WMO instance's LOCAL space
    // (transform via kMulVecMatrix and wmo::kOffInstanceCollisionMatrix first). Returns true AND
    // inOutMaxFraction shrunk below its input value when the ray hit real geometry within that fraction
    // (section 21).
    constexpr uintptr_t kGetTris = 0x007CB0C0;
    using GetTrisFn = bool(__thiscall*)(void* groupObject, const float raySegmentLocal[6],
                                         float* inOutMaxFraction, int param4, int param5,
                                         uint8_t* scratchByte, int param7);

    // operator_mul_C3Vector_C44Matrix: __cdecl(C3Vector* out, const C3Vector* v, const float m[16]) ->
    // out = v * m (row-vector convention). The native engine's own world<->local transform helper, reused
    // as-is rather than reimplemented (section 22.3).
    constexpr uintptr_t kMulVecMatrix = 0x004C21B0;
    using MulVecMatrixFn = void(__cdecl*)(float out[3], const float v[3], const float m[16]);

    // CWorldScene::CullMapObjDefGroupFromExterior: __thiscall(this=defInstance, groupEntry, frustumCorners,
    // flag). Its ONE caller (CWorldScene::CullMapObjDefGroups, the cell+0x14 list walk) already resolves and
    // validates defInstance (via groupEntry+0x20 -> +0x8) before making this call -- section 24 of
    // wmo_portal_camera_cull.md found the ORIGINAL clip-buffer stamp (kGroupClipStamp above) is reached via
    // a different, unrelated list (cell+0x44) whose groupEntry+0x20 is not guaranteed valid, crashing three
    // live attempts to resolve defInstance from there directly. This function is the correct hook point for
    // any Tier-2 logic needing a proven-valid defInstance/groupEntry pair: detour as a call-through (call
    // original first, preserving its own frustum-cull/exterior-stamp/portal-walk work), then use the two
    // pointers this hook already receives as arguments -- zero new dereferences of groupEntry+0x20 needed.
    constexpr uintptr_t kCullMapObjDefGroupFromExterior = 0x007B3A10;
    using CullMapObjDefGroupFromExteriorFn = void(__fastcall*)(void* defInstance, void* edx, void* groupEntry,
                                                                 float* frustumCorners, int flag);
}
