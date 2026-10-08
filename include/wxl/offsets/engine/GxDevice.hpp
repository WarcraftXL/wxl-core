// The engine graphics device (CGxDevice) as a backend implements it: object fields, the format it is
// created from, the pool/buffer/texture/shader records it owns, and the base-class functions it calls.
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

#include "wxl/offsets/engine/Gx.hpp"

// INTERNAL to the core and the SDK (wxl/game/GxDevice.hpp). Read off the D3D9 backend
// (vtable 0x00A2E718) and the base class (vtable 0x00A2DDC0): what a replacement backend has to keep
// in the object, and the engine functions every stock backend shares. Gx.hpp keeps the landmarks the
// render hooks use and owns every address it already names -- this header defers to it rather than
// spelling the same address twice, and static_asserts the two agree wherever they overlap.
namespace wxl::offsets::engine::gxdevice
{
    namespace gx = wxl::offsets::engine::gx;

    // --- object -------------------------------------------------------------------------------------
    constexpr size_t   kD3d9ObjectSize   = 0x3EA4; // CGxDevice::NewD3d allocation
    constexpr size_t   kD3d9ExObjectSize = 0x3F28; // CGxDevice::NewD3d9Ex allocation, the largest stock one
    constexpr unsigned kVTableSlots      = 84;

    // Vtable slots the core drives itself.
    constexpr unsigned kSlotScalarDelete = 8;  // (uint8 flags), ret 4
    constexpr unsigned kSlotDeviceCreate = 10; // (WNDPROC, const CGxFormat*) -> int, ret 8

    // CGxDevice::CGxDevice (this): the base constructor -- matrix stacks, TSLists, render-state arrays,
    // the six shader hash tables. It leaves kApi at 6 and creates no pools.
    constexpr uintptr_t kDeviceCtor = 0x00688690;
    using DeviceCtorFn = void*(__fastcall*)(void* device, void* edx);

    // CGxDevice::DeviceCreatePools (this): the two default stream pools (vertex and index).
    constexpr uintptr_t kDeviceCreatePools = 0x00687940;
    // CGxDevice::DeviceCreateStreamBufs (this): the default stream CGxBufs on those pools. Every stock
    // backend calls both from its constructor, and the shader system's shutdown releases through those
    // bufs -- a device without them crashes on exit.
    constexpr uintptr_t kDeviceCreateStreamBufs = 0x00687900;
    using DeviceCreatePoolsFn = void(__fastcall*)(void* device, void* edx);

    // CGxDevice::~CGxDevice (this): frees the engine-side lists and pools, resets the vptr to the base.
    constexpr uintptr_t kDeviceDtor = 0x006890C0;
    using DeviceDtorFn = void(__fastcall*)(void* device, void* edx);

    // CGxDevice::v_table: the base class table a backend copies and overrides. The D3D9 table a render
    // hook walks is gx::kGxDeviceVTable.
    constexpr uintptr_t kBaseVTable = 0x00A2DDC0;
    // CGxDeviceD3d9Ex::v_table, the second D3D-family table; the D3D9 one is gx::kGxDeviceVTable.
    // (OpenGL has a third at 0x00A2E198, unnamed here because nothing in the core reaches for it.)
    constexpr uintptr_t kD3d9ExVTable = 0x00A2F500;

    // All four tables are exactly kVTableSlots entries and nothing precedes slot 0: the dword at
    // `table + kVTableSlots * 4` is string data in every one (".\CGxDeviceD3d9Ex", "CGxDeviceD3d",
    // "Unfreed texture "), and `table - 4` is not a code address, so there is no RTTI locator to
    // carry along when copying one.
    //
    // **24 of the base table's slots are __purecall (0x0040BAA5)**, not 23: slots 1, 17, 20, 21, 22,
    // 35, 36, 52, 59, 60, 64-67, 71, 72, 76-83. That is the reason kBaseVTable and kDeviceCtor
    // cannot be used to inherit D3D behaviour -- there is no stock implementation behind those slots
    // to forward to, and the 36 the D3D9 table overrides read fields (+0x397C the IDirect3DDevice9,
    // +0x3980 the D3DCAPS9, the cached surfaces and the vertex-declaration cache) that the base
    // constructor never fills. A backend that wants the stock D3D behaviour under its own table has
    // to let kNewD3d build the object and then replace the vptr.
    constexpr uintptr_t kPureCall = 0x0040BAA5;

    // CGxDevice::NewD3d / NewD3d9Ex: the engine's own factories, and the only way to get a
    // fully-formed CGxDeviceD3d. Nine instructions --
    // `SMemAlloc(kD3d9ObjectSize, tag, 0x81, 0)` then a tail jump to the backend constructor, which
    // returns `this`. __cdecl, no arguments, object in EAX, null when the allocation failed.
    //
    // The allocation line 0x81 matters: the matching SMemFree is the one inside vtable slot 8, so an
    // object allocated any other way cannot be deleted through the engine's own path.
    constexpr uintptr_t kNewD3d    = 0x00689EF0; // tag 0x00A2DFDC, ctor kD3d9Ctor
    constexpr uintptr_t kNewD3d9Ex = 0x0068C220; // tag 0x00A2E2E8, ctor kD3d9ExCtor
    using NewDeviceFn = void*(__cdecl*)();

    // The backend constructors the factories tail-jump to. Each runs kDeviceCtor, installs its own
    // table, writes kApi (1 or 2), zeroes its own fields, and ends with kDeviceCreatePools +
    // kDeviceCreateStreamBufs -- the pair whose absence crashes the client on exit.
    constexpr uintptr_t kD3d9Ctor   = 0x0068FD50;
    constexpr uintptr_t kD3d9ExCtor = 0x006A1A90;

    // GxDevCreate(api, wndProc, context): stores the new device in gx::kGxDevicePtr and calls its
    // kSlotDeviceCreate; on failure deletes it through kSlotScalarDelete. Where a backend takes over.
    constexpr uintptr_t kDevCreate = 0x00681290;
    using DevCreateFn = void*(__cdecl*)(int api, void* wndProc, void* context);

    // --- fields ---------------------------------------------------------------------------------------
    // Rects are CRect {minY, minX, maxY, maxX} floats: height at +0x8, width at +0xC. Gx.hpp owns the
    // width and height fields of both; these are the rect bases the whole rect is read or copied from.
    constexpr size_t kDefWindow = 0x164;
    constexpr size_t kCurWindow = 0x174;   // what CGxDevice::DeviceCurWindow returns
    static_assert(kDefWindow + 0x0C == gx::kDefWindowWidth, "defWindow rect base");
    static_assert(kCurWindow + 0x0C == gx::kCurWindowWidth, "curWindow rect base");

    constexpr size_t kApi    = 0x1B4;   // 0 OpenGL, 1 D3D9, 2 D3D9Ex; the base ctor leaves 6
    constexpr size_t kFormat = 0x1BC;   // the active CGxFormat, kFormatSize bytes

    // Written by each backend's ctor: D3D 0 / 1, OpenGL 1 / 0. Meaning not read.
    constexpr size_t kApiFlag228 = 0x228;
    constexpr size_t kApiFlag22C = 0x22C;

    // The lowest mip level the device uses, which DeviceSetBaseMipLevel sets: a texture's upload skips
    // every level above it, so its image's level 0 is this level of the chain.
    constexpr size_t kBaseMipLevel = 0x350;

    // Gamma ramps, 3 x 256 WORD. The live one is what DeviceSetGamma writes; the desktop one is
    // saved at create so it can be put back.
    constexpr size_t kGammaRamp        = 0x354;
    constexpr size_t kGammaRampDesktop = 0x954;
    constexpr size_t kGammaRampBytes   = 0x600;

    constexpr size_t kWndProc       = 0xF54; // the client WNDPROC the backend window proc chains to
    constexpr size_t kContext       = 0xF58; // non-zero while the device is usable
    constexpr size_t kFramePoisoned = 0xF5C; // a buffer lock failed: draws skipped until ScenePresent clears it
    constexpr size_t kSizeTracking  = 0xF60; // set by a size event unless minimized
    constexpr size_t kHasFocus      = 0xF64;
    constexpr size_t kFrameCounter  = 0xF68; // presented frames, bumped by the base ScenePresent
    // gx::kViewportDirty (0xF6C) completes this block.

    // The viewport as the engine states it: six normalized floats, all 0..1, written by
    // GxXformSetViewport. A backend turns them into pixels against kCurWindow.
    constexpr size_t kViewportMinX = 0xF70;
    constexpr size_t kViewportMaxX = 0xF74;
    constexpr size_t kViewportMinY = 0xF78;
    constexpr size_t kViewportMaxY = 0xF7C;
    constexpr size_t kViewportMinZ = 0xF80;
    constexpr size_t kViewportMaxZ = 0xF84;

    // The projection in the API's own depth range, derived by the D3D XformSetProjection from the
    // engine's OpenGL-depth one (gx::kDeviceProjection), and its dirty byte.
    constexpr size_t kApiProjection      = 0xFC8;
    constexpr size_t kApiProjectionDirty = 0x19E4;

    // The 19 CGxMatrixStack objects: 0-7 the application texture matrices, 8 world, 9 projection
    // (only its dirty byte is used), 10 view, 11-18 one texgen matrix per texture stage. Each holds
    // {top index, dirty byte, 4 matrices of 0x40, 4 flag words}; bit 0 of a flag means identity.
    constexpr size_t kMatrixStacks      = 0x1008;
    constexpr size_t kMatrixStackStride = 0x118;
    constexpr size_t kMatrixStackTop    = 0x00; // uint32 index, 0..3
    constexpr size_t kMatrixStackDirty  = 0x04; // uint8
    constexpr size_t kMatrixStackBase   = 0x08; // 4 x C44Matrix
    constexpr size_t kMatrixStackFlags  = 0x108; // uint32 per level
    constexpr uint32_t kMatrixStackFlagIdentity = 0x1; // that level is identity; GxXformPush clears it, Identity() sets it
    constexpr unsigned kMatrixStackDepth = 4;
    constexpr unsigned kXformTex0        = 0;
    constexpr unsigned kXformWorld       = 8;
    constexpr unsigned kXformProjection  = 9;
    constexpr unsigned kXformView        = 10;
    constexpr unsigned kXformTexGen0     = 11;
    constexpr unsigned kXformCount       = 19;
    // Gx.hpp reaches the view stack directly for the save/restore the world render needs.
    static_assert(kMatrixStacks + kXformView * kMatrixStackStride == gx::kDeviceViewIndex, "view stack");
    static_assert(kMatrixStacks + kXformView * kMatrixStackStride + kMatrixStackBase == gx::kDeviceViewBase,
                  "view stack matrices");
    static_assert(kMatrixStackStride - kMatrixStackBase >= gx::kDeviceViewStride, "view stack stride");

    // Six user clip planes of four floats, and the mask saying which were written.
    constexpr size_t kClipPlaneDirty  = 0x24D0;
    constexpr size_t kClipPlanes      = 0x24D4;
    constexpr unsigned kClipPlaneCount = 6;

    // The scissor rect, normalized like the viewport, and its dirty flag.
    constexpr size_t kScissorDirty = 0x2534;
    constexpr size_t kScissorRect  = 0x2538;

    // Four fixed-function light slots: {C4Vector position (w == 1) or direction, ambient, diffuse,
    // specular RGB floats, attenuation, enabled, dirty}.
    constexpr size_t kLights       = 0x2548;
    constexpr size_t kLightStride  = 0x48;
    constexpr unsigned kLightCount = 4;
    constexpr size_t kLightPosition    = 0x00;
    constexpr size_t kLightAmbient     = 0x10;
    constexpr size_t kLightDiffuse     = 0x1C;
    constexpr size_t kLightSpecular    = 0x28;
    constexpr size_t kLightAttenuation = 0x34;
    constexpr size_t kLightEnabled     = 0x40;

    // One TSHashTable<CGxShader> per EGxShTarget, where ShaderCreate interns by name.
    constexpr size_t kShaderTables      = 0x2668;
    constexpr size_t kShaderTableStride = 0x28;

    // TSList of every CGxPool: {link offset, terminator {prev, next}}. The base constructor
    // (kDeviceCtor, 0x00688D30..0x00688D41) writes the link offset as 0 -- the link is at the head of
    // CGxPool -- and points the terminator at itself (prev = +0x2764, next = +0x2764 | 1). So the
    // dword at kPoolList is 0 on a constructed device; the terminator's two links are what say the
    // constructor ran (pools added later keep them non-null).
    constexpr size_t kPoolList          = 0x2760;
    constexpr size_t kPoolListPrev      = 0x2764; // terminator prev link: never 0 once constructed
    constexpr size_t kPoolListNext      = 0x2768; // terminator next link (low bit 1 = terminator)
    constexpr size_t kLockedBuf         = 0x2778; // + pool type * 4: the CGxBuf currently locked
    constexpr size_t kStreamPools       = 0x2780; // + pool type * 4: the two default stream pools
    constexpr size_t kStreamBufs        = 0x2788; // + pool type * 4: their default CGxBufs

    // The vertex attribute slots a draw reads: one descriptor per EGxVertexAttrib, the CGxBuf feeding
    // it, and the masks saying which are enabled and which changed. Gx.hpp owns the buffer array.
    constexpr size_t kAttribSlots       = 0x2790;
    constexpr size_t kAttribSlotStride  = 0x10;
    constexpr size_t kAttribSlotAttrib  = 0x00; // EGxVertexAttrib
    constexpr size_t kAttribSlotFormat  = 0x04; // attribute data format
    constexpr size_t kAttribSlotOffset  = 0x08; // byte offset inside the vertex
    constexpr size_t kAttribSlotStrideB = 0x0C; // bytes between vertices
    constexpr unsigned kAttribCount     = 14;
    constexpr size_t kAttribBufs        = gx::kGxDeviceVertexStream; // + attribute * 4 -> CGxBuf*
    constexpr size_t kAttribEnabled     = 0x28A8; // bit per attribute
    constexpr unsigned kAttribBitColor0 = 4;      // its Color0 bit: the fixed formats' masks are 0x11 PC, 0x51 PCT, 0x41 PT
    constexpr unsigned kAttribBitNormal = 3;      // its Normal bit (EGxVertexAttrib 3): 0x19 PNC, 0x59 PNCT, 0xD9 PNCT2
    constexpr size_t kAttribChanged     = 0x28AC;
    constexpr size_t kVertexFormat      = 0x28B0; // EGxVertexBufferFormat, or kVertexFormatCustom
    constexpr size_t kVertexBuf         = 0x28B4; // the CGxBuf of a fixed format
    constexpr size_t kVertexStride      = 0x28B8;
    constexpr size_t kIndexBuf          = 0x28BC;
    constexpr size_t kIndexBufChanged   = 0x28C0;
    constexpr uint32_t kVertexFormatCustom = 14;  // the slots describe the layout instead of a format

    constexpr size_t kEmergencyLockBlock = 0x28C4; // + pool type * 0x14: the EmergencyMem object IBufLock 0x68FB10 locks
    constexpr size_t kEmergencyLock     = 0x28D4; // + pool type * 0x14: that object's +0x10 "in use" byte, read by IBufUnlock

    // The render states. kRsCount entries of kRsStride bytes live behind the POINTER at kRsTable --
    // value first, then the push depth it was saved at, then its dirty flag. kRsShadow is the
    // backend-side copy IRsSync compares against before it calls slot 1.
    constexpr size_t kRsCount         = 0x28F0; // uint32, = kRenderStateCount
    constexpr size_t kRsTable         = 0x28F4; // -> the state array
    constexpr size_t kRsShadowCount   = 0x28FC;
    constexpr size_t kRsShadow        = 0x2900; // -> the hardware shadow, kRsShadowStride each
    constexpr size_t kRsStride        = 0x18;
    constexpr size_t kRsShadowStride  = 0x10;
    constexpr size_t kRsValue         = 0x00; // 16 bytes: int, float, pointer, or a C3Vector
    constexpr size_t kRsSavedDepth    = 0x10;
    constexpr size_t kRsDirty         = 0x14;
    constexpr unsigned kRenderStateCount = 86;

    // EGxRenderState ids, as the D3D backend's own handler switches on them: what a backend reads to
    // build one draw. The ids are the engine's enum, not offsets, so they live here whole.
    enum : unsigned
    {
        kRsPolygonOffset   = 0,
        kRsMatDiffuse      = 1,
        kRsMatEmissive     = 2,
        kRsMatSpecular     = 3,
        kRsMatSpecularExp  = 4,
        kRsNormalizeNormals = 5,
        kRsBlend           = 6,
        kRsAlphaRef        = 7,  // 0..255; below 1 the test is off
        kRsFogStart        = 8,
        kRsFogEnd          = 9,
        kRsFogColor        = 10,
        kRsLighting        = 11,
        kRsFog             = 12,
        kRsDepthTest       = 13,
        kRsDepthFunc       = 14,
        kRsDepthWrite      = 15,
        kRsColorWrite      = 16, // engine mask: bit 0 R, bit 1 B, bit 2 G, bit 3 A
        kRsCulling         = 17,
        kRsClipPlaneMask   = 18,
        kRsMultisample     = 19,
        kRsScissorTest     = 20,
        kRsTexture0        = 21, // + stage, up to 15
        kRsColorOp0        = 37, // + stage, up to 7
        kRsAlphaOp0        = 45,
        kRsTexGen0         = 53,
        kRsTexTransform0   = 61,
        kRsTexCoord0       = 69,
        kRsVertexShader    = 77,
        kRsPixelShader     = 78,
        kRsPointSize       = 79,
        kRsPointScale      = 80,
        kRsPointSizeMin    = 81,
        kRsPointSizeMax    = 82,
        kRsPointSprite     = 83,
        kRsBlendFactor     = 84,
        kRsColorMaterial   = 85,
    };

    // Master-enable bits (slot 51): a bit that is off forces its state to the disabled value.
    enum : unsigned
    {
        kMasterLighting   = 0,
        kMasterFog        = 1,
        kMasterDepthTest  = 2,
        kMasterDepthWrite = 3,
        kMasterColorWrite = 4,
        kMasterCulling    = 5,
        kMasterProjection = 7, // read by the D3D XformSetProjection
        kMasterSolidFill  = 8, // clear = wireframe
    };

    // EGxBlend, the blend modes state kRsBlend selects. Modes 0 and 1 do not blend; 1 keys on alpha.
    constexpr uint32_t kBlendModeCount = 12;

    // EGxDepthFunc, state kRsDepthFunc.
    enum : uint32_t
    {
        kDepthFuncLessEqual    = 0,
        kDepthFuncEqual        = 1,
        kDepthFuncGreaterEqual = 2,
        kDepthFuncLess         = 3,
    };

    // EGxCull, state kRsCulling. Mode 1 culls clockwise triangles in screen space, so a front face is
    // counter-clockwise there.
    enum : uint32_t
    {
        kCullNone = 0,
        kCullCw   = 1,
        kCullCcw  = 2,
    };

    // EGxPrim, the primitive a CGxBatch names.
    enum : uint32_t
    {
        kPrimPoints        = 0,
        kPrimLines         = 1,
        kPrimLineStrip     = 2,
        kPrimTriangles     = 3,
        kPrimTriangleStrip = 4,
        kPrimTriangleFan   = 5,
    };

    // --- CGxBatch, the argument to the Draw slot ---------------------------------------------------
    // 0x10 bytes, built on the CALLER's stack and never owned by the device -- so a backend that
    // wants it past the call has to copy it. The D3D Draw (0x006A3620) reads exactly these four
    // fields and nothing else; the UI batch builder at 0x00484B00 writes
    // {kPrimTriangles, 0, indexCount, 0, vertexCount - 1}.
    constexpr size_t kBatchSize    = 0x10;
    constexpr size_t kBatchPrim    = 0x00; // uint32, an EGxPrim above
    constexpr size_t kBatchStart   = 0x04; // uint32, first index, relative to the bound index buffer
    constexpr size_t kBatchCount   = 0x08; // uint32, index count -- vertex count when not indexed
    constexpr size_t kBatchMinVert = 0x0C; // uint16
    constexpr size_t kBatchMaxVert = 0x0E; // uint16

    // CGxDevice::PrimCalcCount(prim, count) -> primitive count. Whole body:
    // `count / kPrimDivisors[prim] - kPrimAdjust[prim]`, with the division skipped when the divisor
    // is 1. Both tables are six dwords indexed by EGxPrim, read out of .rdata:
    //   divisors {1, 2, 1, 3, 1, 1}   adjust {0, 0, 1, 0, 2, 2}
    // which is what turns an index count into a primitive count rather than the other way round --
    // worth having named, because inverting it overstates a triangle list by 3x.
    //
    // Despite the CGxDevice:: name it takes NO `this`: the prologue is
    // `mov eax,[ebp+0xC]` / `mov esi,[ebp+8]`, so both arguments come off the stack, and it is
    // __stdcall (`ret 8`), not __thiscall. Calling it with a device in ecx would read `prim` from
    // whatever the caller happened to push.
    constexpr uintptr_t kPrimCalcCount = 0x00682F40;
    constexpr uintptr_t kPrimDivisors  = 0x00AD8B4C; // uint32[6], by EGxPrim
    constexpr uintptr_t kPrimAdjust    = 0x00AD8B64; // uint32[6], by EGxPrim
    using PrimCalcCountFn = uint32_t(__stdcall*)(uint32_t prim, uint32_t count);

    // EGxVertexAttrib, which the attribute slots are indexed by.
    enum : unsigned
    {
        kAttribPosition     = 0,
        kAttribBlendWeight  = 1,
        kAttribBlendIndices = 2,
        kAttribNormal       = 3,
        kAttribColor0       = 4,
        kAttribColor1       = 5,
        kAttribTexCoord0    = 6, // + set, up to 7
    };

    // Attribute data formats, what a slot's format field holds.
    enum : uint32_t
    {
        kAttribFmtColorBgra = 0,
        kAttribFmtUByte4    = 1,
        kAttribFmtUByte4N   = 2,
        kAttribFmtFloat2    = 3,
        kAttribFmtFloat3    = 4,
        kAttribFmtShort2    = 5,
        kAttribFmtFloat1    = 6,
    };

    constexpr size_t kTexList           = 0x2904; // TSList of every CGxTex
    constexpr size_t kTexListHead       = 0x290C;

    // The render-target table, {CGxTex*, cube face, API surface} per EGxBuffer (0 colour, 1 depth).
    // Gx.hpp owns the two entries a render hook tests.
    constexpr size_t kRenderTargets      = 0x2910;
    constexpr size_t kRenderTargetStride = 0x0C;
    constexpr size_t kRenderTargetTex    = 0x00;
    constexpr size_t kRenderTargetFace   = 0x04;
    constexpr size_t kRenderTargetApi    = 0x08;
    constexpr unsigned kBufferColor      = 0;
    constexpr unsigned kBufferDepth      = 1;
    static_assert(kRenderTargets + kBufferColor * kRenderTargetStride + kRenderTargetApi == gx::kRtOverrideField,
                  "colour render-target surface");

    constexpr size_t kScreenshotPending = 0x2934; // set by DeviceTakeScreenShot, cleared by ScenePresent
    constexpr size_t kScreenshotWidth   = 0x2938;
    constexpr size_t kScreenshotHeight  = 0x293C;

    constexpr size_t kCursorVisible   = 0x2950;
    constexpr size_t kCursorHardware  = 0x2954; // the format asked for one and the caps allow it
    constexpr size_t kCursorHotspotX  = 0x2958;
    constexpr size_t kCursorHotspotY  = 0x295C;
    constexpr size_t kCursorImage     = 0x2960; // 32 x 32 uint32, what CursorLock returns
    constexpr size_t kCursorTexture   = 0x3960; // the software cursor's CGxTex

    // Backend window state, at the same place in every stock backend.
    constexpr size_t kWindow           = 0x3968; // HWND
    constexpr size_t kWindowClassAtom  = 0x396C; // uint16, from kWindowClassCreate
    constexpr size_t kOwnsWindow       = 0x3970; // 1 when the device created the window itself

    // The D3D backends' API objects (CGxDeviceD3d and CGxDeviceD3d9Ex, same place in both). The
    // reference's layout table puts d3d9.dll's module, the IDirect3D9 and the IDirect3DDevice9 in
    // the three dwords after kOwnsWindow; the surfaces are section 9.3's cached ones, and the
    // placeholder is the 8x8 texture ITexForceRecreation (0x006A2AA0) points every dropped
    // CGxTex's kTexApiObject at until ITexCreate runs again.
    constexpr size_t kD3dModule          = 0x3974; // HMODULE of d3d9.dll
    constexpr size_t kD3d9               = 0x3978; // IDirect3D9*
    constexpr size_t kD3dDevice          = 0x397C; // IDirect3DDevice9*; = gx::kD3DDeviceField
    constexpr size_t kD3dCaps            = 0x3980; // D3DCAPS9, the backend's copy
    constexpr size_t kD3dOffscreenDepth  = 0x3B38; // depth surface for colour targets without a depth texture (0x006A7940)
    constexpr size_t kD3dBackBuffer      = 0x3B3C; // GetRenderTarget(0) at IStateSetD3DDefaults; = gx::kBackBufferField
    constexpr size_t kD3dDepthStencil    = 0x3B40; // GetDepthStencilSurface, the auto depth buffer; = gx::kDepthSurfaceField
    constexpr size_t kD3dResolveSurface  = 0x3B44; // MSAA resolve surface for DeviceReadPixels (0x0068F6A0)
    constexpr size_t kD3dEventQuery      = 0x3B48; // IDirect3DQuery9, D3DQUERYTYPE_EVENT
    constexpr size_t kD3dPlaceholderTex  = 0x3B58; // 8x8 IDirect3DTexture9 made in ICreateD3dDevice (0x0068F3D0)
    // Set to 1 by DeviceSetFormat (0x006904D0), DeviceWM focus-in (0x00690230) and CursorUnlock
    // (0x0068E7E0): "the cursor must be re-applied". The one D3D-region field the engine's own reused
    // code writes on a device that never creates D3D (docs/native-device-plan.md 1.4).
    constexpr size_t kD3dCursorDirty     = 0x3B4C;
    static_assert(kD3dDevice == gx::kD3DDeviceField, "the D3D device pointer");
    static_assert(kD3dBackBuffer == gx::kBackBufferField, "the cached back buffer");
    static_assert(kD3dDepthStencil == gx::kDepthSurfaceField, "the cached depth-stencil surface");

    // --- capabilities ---------------------------------------------------------------------------------
    // Filled by the D3D backend's ISetCaps (0x0068EE20) from D3DCAPS9 and CheckDeviceFormat. Engine
    // code reads the block through CGxDevice::Caps, which returns the device plus kCapsBase.
    constexpr size_t kCapsBase              = 0x214;
    constexpr size_t kCapsTextureUnits      = 0x214; // MaxSimultaneousTextures, capped at 8
    constexpr size_t kCaps218               = 0x218; // D3D writes 0
    constexpr size_t kCaps21C               = 0x21C; // D3D writes 1
    constexpr size_t kCapsMaxStreams        = 0x220;
    // gx::kGxDeviceBaseVertexMode (0x224) is this block's stream-offset mode.
    constexpr size_t kCapsMaxVertexIndex    = 0x230;
    constexpr size_t kCapsAutoGenMipmaps    = 0x234;
    constexpr size_t kCapsTexFormats        = 0x238; // uint32[kTexFormatCount], indexed by EGxTexFormat
    constexpr size_t kCapsTex2D             = 0x26C;
    constexpr size_t kCapsTexCube           = 0x270;
    constexpr size_t kCapsTex274            = 0x274; // D3D writes 0
    constexpr size_t kCapsTexNpot           = 0x278; // 0 only when textures must be powers of two
    constexpr size_t kCapsTexNpotConditional = 0x27C; // D3D: NONPOW2CONDITIONAL, forced 0 on NVIDIA
    constexpr size_t kCapsMaxTexWidth       = 0x280;
    constexpr size_t kCapsMaxTexHeight      = 0x284;
    constexpr size_t kCapsMaxCubeSize       = 0x288;
    constexpr size_t kCapsMaxTexExtent      = 0x28C;
    constexpr size_t kCapsRtArgb8888        = 0x298;
    constexpr size_t kCapsRtRgb565          = 0x2A4;
    constexpr size_t kCapsRtRg16f           = 0x2B8;
    constexpr size_t kCapsRtR32f            = 0x2BC;
    constexpr size_t kCapsDepthTexD24S8     = 0x2C0;
    // Per EGxShTarget (0 vertex .. 4 pixel, 6 entries): the supported profile, then the constant count.
    constexpr size_t kCapsShaderTarget      = 0x2C8;
    constexpr size_t kCapsShaderConstants   = 0x2E0;
    constexpr unsigned kShaderTargets       = 6;
    constexpr size_t kCapsTexFilterTrilinear = 0x2F8;
    constexpr size_t kCapsTexFilterAniso    = 0x2FC;
    constexpr size_t kCapsMaxAnisotropy     = 0x300;
    constexpr size_t kCapsDepthBias         = 0x304;
    constexpr size_t kCapsColorWrite        = 0x308;
    constexpr size_t kCapsMaxClipPlanes     = 0x30C;
    constexpr size_t kCapsHwCursor          = 0x310;
    constexpr size_t kCapsOcclusionQuery    = 0x314;
    constexpr size_t kCapsPointParameters   = 0x318;
    constexpr size_t kCapsMaxPointSize      = 0x31C; // float
    constexpr size_t kCapsPointSprites      = 0x320;
    constexpr size_t kCapsBlendFactor       = 0x324;
    constexpr size_t kCaps328               = 0x328; // 0x328..0x338: D3D writes 0
    constexpr size_t kCaps338               = 0x338;
    constexpr size_t kCaps344               = 0x344; // D3D writes 1
    // ISetCaps' last act, literally `kCaps348 = kCaps34C = (pixelProfile != kPixelProfilePs30)`.
    // Not cosmetic: CWorldScene::RenderChunks reads kCaps348 to decide whether the terrain colour
    // travels as pixel constant c2 or as render state 10, and CDetailDoodad's constant setup and
    // CShaderEffect::SetFogParams branch on it too. Anything that rewrites the pixel profile has to
    // re-derive both, which the stock path does and the DeviceOverride path does not.
    constexpr size_t kCaps348               = 0x348;
    constexpr size_t kCaps34C               = 0x34C; // same value as kCaps348

    // Profiles as kCapsShaderTarget stores them. A backend chooses which .bls family the engine loads
    // by what it writes here: the path is built from the profile name of the level it finds.
    constexpr uint32_t kVertexProfileVs11 = 1;
    constexpr uint32_t kVertexProfileVs20 = 2;
    constexpr uint32_t kVertexProfileVs30 = 3;
    constexpr uint32_t kPixelProfilePs11  = 1;
    constexpr uint32_t kPixelProfilePs14  = 2;
    constexpr uint32_t kPixelProfilePs20  = 3;
    constexpr uint32_t kPixelProfilePs30  = 4;
    constexpr unsigned kShTargetVertex    = 0;
    constexpr unsigned kShTargetPixel     = 4;

    // --- where the caps block comes from, and what can rewrite it afterwards ---------------------
    // ISetCaps derives the whole block from the adapter's D3DCAPS9 plus CheckDeviceFormat probes. It
    // has exactly one caller each (inside the matching ICreateD3dDevice), and the first shader in the
    // process is created a few statements after it returns -- which makes a post-call detour here the
    // one point where the derived levels can be corrected before anything reads them.
    //
    // The pixel profile is clamped to kPixelProfilePs20 unless the vertex profile reached vs_3_0:
    // ISetCaps ends with `if (pixel == 4 && vertex != 3) pixel = 3;`. CGxFormat's own caps fields can
    // only ever LOWER a level (the test is `if (v != -1 && v <= current) current = v`).
    constexpr uintptr_t kD3d9SetCaps   = 0x0068EE20; // CGxDeviceD3d::ISetCaps
    constexpr uintptr_t kD3d9ExSetCaps = 0x006A0B40; // the D3D9Ex backend's own copy
    using SetCapsFn = void(__fastcall*)(void* device, void* edx, const void* format);

    // Vtable slot 28, shared by the D3D9 and D3D9Ex tables. Override 0 writes its payload straight
    // into the pixel profile and does NOT re-derive kCaps348/kCaps34C. ConsoleDeviceInitialize runs
    // it for every entry the hardware-detection row or -gxOverride set, after the device exists, so
    // it can undo a profile correction made at ISetCaps time.
    //
    // It can only ever lower the profile: ConsoleGxOverride (0x007696D0) maps the user's number
    // through a table reaching levels 1, 2, 3, 7, 8, 9, 10, 12 and 13 -- never 4. So there is no
    // route to ps_3_0 through the CVar, only away from it.
    constexpr uintptr_t kDeviceOverride       = 0x0069FF40;
    constexpr unsigned  kOverridePixelProfile = 0; // the EGxOverride whose payload lands in kCapsShaderTarget[4]
    using DeviceOverrideFn = void(__fastcall*)(void* device, void* edx, int index, uint32_t value);

    // The adapter's own D3DCAPS9, copied onto the device object: hardware truth, as opposed to the
    // derived block above. Named so a caller can gate on what the driver really reports without
    // pulling in d3d9.h. Offsets inside it are D3DCAPS9 member indices 49, 50 and 51.
    constexpr size_t kD3dCaps9                    = 0x3980;
    constexpr size_t kCaps9VertexShaderVersion    = 0xC4;
    constexpr size_t kCaps9MaxVertexShaderConst   = 0xC8;
    constexpr size_t kCaps9PixelShaderVersion     = 0xCC;
    // The version words ISetCaps itself compares against, as D3DVS_VERSION / D3DPS_VERSION build them.
    constexpr uint32_t kVsVersionTag = 0xFFFE0000;
    constexpr uint32_t kPsVersionTag = 0xFFFF0000;
    constexpr uint32_t kVsVersion30  = 0xFFFE0300;
    constexpr uint32_t kPsVersion30  = 0xFFFF0300;

    // --- CGxFormat (kFormatSize bytes) ------------------------------------------------------------------
    // Offsets INSIDE the format record. The active copy lives at the device's kFormat, so Gx.hpp's
    // backbuffer size constants are kFormat plus the width and height below.
    constexpr size_t kFormatSize          = 0x58;
    constexpr size_t kFmtHwVertexProcessing = 0x04; // uint8
    constexpr size_t kFmtHwCursor         = 0x05; // uint8
    constexpr size_t kFmtGpuSync          = 0x06; // uint8: sync at present ("fix lag")
    constexpr size_t kFmtWindowed         = 0x07; // uint8
    constexpr size_t kFmtKeepAspect       = 0x08; // uint8: the base SetFormat derives kWinAspect from it
    constexpr size_t kFmtWindowStyle      = 0x0C; // 1 fullscreen window, 2 popup, else overlapped
    constexpr size_t kFmtDepthFormat      = 0x10; // EGxFormat
    constexpr size_t kFmtWidth            = 0x14;
    constexpr size_t kFmtHeight           = 0x18;
    constexpr size_t kFmtBackBuffers      = 0x1C;
    constexpr size_t kFmtSamples          = 0x20;
    constexpr size_t kFmtSampleQuality    = 0x24; // float 0..1
    constexpr size_t kFmtColorFormat      = 0x28; // EGxFormat
    constexpr size_t kFmtRefreshRate      = 0x2C;
    constexpr size_t kFmtVSync            = 0x30; // present interval, 0 = immediate
    constexpr size_t kFmtStereo           = 0x34; // uint8
    constexpr size_t kFmtMaxVertexProfile = 0x38; // int, -1 = no cap
    constexpr size_t kFmtMaxPixelProfile  = 0x48; // int, -1 = no cap
    constexpr size_t kFmtPositionX        = 0x50;
    constexpr size_t kFmtPositionY        = 0x54;
    static_assert(kFormat + kFmtWidth  == gx::kFormatWidth,  "format width");
    static_assert(kFormat + kFmtHeight == gx::kFormatHeight, "format height");
    constexpr size_t kWindowed = kFormat + kFmtWindowed; // CGxDevice::IDevIsWindowed reads this byte

    // EGxFormat, the colour and depth formats a CGxFormat names (CGxDeviceD3d::s_GxFormatToD3dFormat).
    enum : uint32_t
    {
        kGxFmtRgb565      = 0,
        kGxFmtXrgb8888    = 1,
        kGxFmtArgb8888    = 2,
        kGxFmtArgb2101010 = 3,
        kGxFmtD16         = 4,
        kGxFmtD24X8       = 5,
        kGxFmtD24S8       = 6,
        kGxFmtD32         = 7,
    };

    // EGxTexFormat, what a CGxTex holds (CGxDeviceD3d::s_GxTexFmtToD3dFmt).
    enum : uint32_t
    {
        kTexFmtUnknown  = 0,
        kTexFmtAbgr8888 = 1,
        kTexFmtArgb8888 = 2,
        kTexFmtArgb4444 = 3,
        kTexFmtArgb1555 = 4,
        kTexFmtRgb565   = 5,
        kTexFmtDxt1     = 6,
        kTexFmtDxt3     = 7,
        kTexFmtDxt5     = 8,
        kTexFmtU8V8     = 9,
        kTexFmtG16R16F  = 10,
        kTexFmtR32F     = 11,
        kTexFmtD24X8    = 12,
    };
    constexpr uint32_t kTexFormatCount = 13;

    // --- CGxPool / CGxBuf -------------------------------------------------------------------------------
    // The base class owns both records and allocates buffers inside a pool; the backend owns the pool's
    // memory and keeps its handle in the pool.
    constexpr size_t kPoolType         = 0x08; // 0 vertex, 1 index
    constexpr size_t kPoolUsage        = 0x0C; // 0 static, 1 dynamic, 2 stream
    constexpr size_t kPoolSize         = 0x10; // bytes, set by PoolSizeSet
    constexpr size_t kPoolApiHandle    = 0x14; // the backend's object for the pool
    constexpr size_t kPoolStreamCursor = 0x1C; // stream pools: end of the last buffer placed
    constexpr size_t kPoolBufList      = 0x20; // TSList of the pool's CGxBufs
    constexpr size_t kPoolName         = 0x30;
    constexpr uint32_t kPoolTypeVertex  = 0;
    constexpr uint32_t kPoolTypeIndex   = 1;
    constexpr uint32_t kPoolUsageStatic = 0;
    constexpr uint32_t kPoolUsageDynamic = 1;
    constexpr uint32_t kPoolUsageStream = 2;
    constexpr unsigned kPoolTypeCount   = 2;

    constexpr size_t kBufPool     = 0x08;                     // -> CGxPool
    constexpr size_t kBufItemSize = gx::kGxBufStreamStride;   // 0x0C
    constexpr size_t kBufItemCount = 0x10;
    constexpr size_t kBufBytes    = 0x14;
    constexpr size_t kBufOffset   = gx::kGxBufStreamOffset;   // 0x18, placed by the lock on stream pools
    constexpr size_t kBufHasData  = 0x1C;                     // uint8: data was written and not discarded
    constexpr size_t kBufValid    = 0x1D;                     // uint8: the last unlock succeeded
    constexpr size_t kBufDirty    = 0x1E;                     // uint8, set by the base BufLock/BufData

    // Index buffers are 16-bit everywhere: the engine has no 32-bit index path.
    constexpr size_t kIndexSize = 2;

    // --- CGxTex -----------------------------------------------------------------------------------------
    constexpr size_t kTexUpdateRect    = 0x00; // CiRect {minY, minX, maxY, maxX}, what to upload next
    constexpr size_t kTexWidth         = 0x14;
    constexpr size_t kTexHeight        = 0x18;
    constexpr size_t kTexDepth         = 0x1C;
    constexpr size_t kTexTarget        = 0x20; // EGxTexTarget, 1 = cube map
    constexpr size_t kTexFormat        = 0x24; // EGxTexFormat the image is stored as
    constexpr size_t kTexDataFormat    = 0x28; // EGxTexFormat the fill callback delivers
    constexpr size_t kTexFlags         = 0x2C; // CGxTexFlags
    constexpr size_t kTexUserArg       = 0x30; // handed back to the fill callback
    constexpr size_t kTexFillCallback  = 0x34; // fills the texels on upload; 0 = cannot update
    constexpr size_t kTexApiObject     = 0x38; // D3D: IDirect3DTexture9*, IDirect3DCubeTexture9* for a cube map
    constexpr size_t kTexApiObject2    = 0x3C; // always 0 on D3D; "created" is tested as +0x38 || +0x3C
    // The TSLink on the device's texture list (kTexList): the next CGxTex is at +0x44; a value with
    // bit 0 set, or 0, ends the walk (ITexForceRecreation 0x006A2AA0, read from the bytes).
    constexpr size_t kTexLinkNext      = 0x44;
    constexpr size_t kTexUpdatePending = 0x5A; // uint8
    constexpr size_t kTexNeedsRecreate = 0x5B; // uint8
    constexpr size_t kTexFlagsChanged  = 0x5C; // uint8

    // CGxTexFlags, the dword at kTexFlags.
    constexpr uint32_t kTexFlagFilterMask   = 0x7;    // EGxTexFilter
    constexpr uint32_t kTexFlagWrapU        = 1 << 3; // repeat, else clamp
    constexpr uint32_t kTexFlagWrapV        = 1 << 4;
    constexpr uint32_t kTexFlagSingleMip    = 1 << 5; // one level, at the device's base mip level
    constexpr uint32_t kTexFlagGenerateMips = 1 << 6; // one level uploaded, the rest derived
    constexpr uint32_t kTexFlagRenderTarget = 1 << 7; // no upload; required by TexCopy / TexStretch
    constexpr unsigned kTexFlagAnisoShift   = 9;      // bits 9-13: max anisotropy
    constexpr uint32_t kTexFlagAnisoMask    = 0x1F;
    constexpr uint32_t kTexFlagRectIsOrigin = 1 << 15; // callback data already points at the update rect

    // EGxTexFilter, the low bits of the flags: {min, mag, mip} per mode.
    enum : uint32_t
    {
        kTexFilterNearest           = 0,
        kTexFilterLinear            = 1,
        kTexFilterNearestMipNearest = 2,
        kTexFilterLinearMipNearest  = 3,
        kTexFilterLinearMipLinear   = 4,
        kTexFilterAnisotropic       = 5,
    };

    // The fill callback a CGxTex carries (__cdecl). cmd 0 opens an upload, 1 asks for one face and
    // level (set *pitch and *data), 2 closes it, 3 says the API object was dropped.
    using TexFillFn = void(__cdecl*)(uint32_t cmd, uint32_t width, uint32_t height, uint32_t face, uint32_t level,
                                     void* userArg, uint32_t* pitch, const void** data);
    constexpr uint32_t kTexCmdBegin   = 0;
    constexpr uint32_t kTexCmdGetData = 1;
    constexpr uint32_t kTexCmdEnd     = 2;
    constexpr uint32_t kTexCmdDropped = 3;

    // --- CGxShader --------------------------------------------------------------------------------------
    constexpr size_t kShaderName       = 0x18; // "Name", or "Name:perm" for one of several permutations
    constexpr size_t kShaderRefCount   = 0x1C;
    constexpr size_t kShaderApiObject  = 0x20;
    constexpr size_t kShaderTarget     = 0x24; // EGxShTarget
    constexpr size_t kShaderValid      = 0x2C; // what CGxShader::Valid returns
    constexpr size_t kShaderCreateTried = 0x30; // CGxShader::Valid calls IShaderCreate while 0
    // +0x48..+0x54 is one TSFixedArray<uint8>: alloc +0x48, SIZE +0x4C, data +0x50, chunk +0x54.
    // So +0x4C is a byte count, not a flag -- it was named kShaderHasCode, which read as a boolean
    // and is how a consumer ends up testing a length for truthiness.
    constexpr size_t kShaderCodeSize   = 0x4C; // bytes in kShaderCode; 0 means the record has none
    constexpr size_t kShaderCode       = 0x50; // the compiled bytecode

    // --- functions every stock backend shares ------------------------------------------------------------
    // Registers "GxWindowClassD3d" with the engine window proc (gx::kWindowProc), the Blizzard icon and
    // cursor. Returns the class atom.
    constexpr uintptr_t kWindowClassCreate = 0x0068EB20;
    using WindowClassCreateFn = uint16_t(__cdecl*)();

    // The D3D backend's window creation (this, CGxFormat*), ret 4. Reads only the format -- style from
    // the windowed flag and style, size, position -- and writes the HWND to kWindow; the device is the
    // window's creation parameter, which the engine window proc stores and later reads back. The format
    // is updated in place for the fullscreen-window styles.
    constexpr uintptr_t kCreateWindow = 0x0068EBB0;
    using CreateWindowFn = bool(__fastcall*)(void* device, void* edx, void* format);

    // The window procedure both D3D window classes register. Every mouse, keyboard and window message
    // of the game goes through it, and it chains to the client WNDPROC at kWndProc.
    constexpr uintptr_t kWindowProc = 0x006A0360;

    // CGxDevice::DeviceSetDefWindow (this, const CRect*), ret 4: sets defWindow and curWindow. The
    // address is gx::kDeviceSetDefWindow, the resolution choke a render hook also owns.
    using DeviceSetDefWindowFn = void(__fastcall*)(void* device, void* edx, const float* rect);

    // CGxDevice::WaitForFPSCap (this): sleeps to honour maxfps / maxfpsbk (the latter while unfocused).
    constexpr uintptr_t kWaitForFpsCap = 0x006836D0;
    using WaitForFpsCapFn = void(__fastcall*)(void* device, void* edx);

    // --- base-class functions a backend that is not D3D calls itself ---------------------------------
    // Everything ICreateD3dDevice (0x0068F3D0) and the D3D scene slots do besides talking to D3D, read
    // in the decompilation, so a native backend can do the same work through the engine's own code.

    // CGxDevice::IRsForceUpdate (this): marks all 86 states dirty and inverts the hardware shadow so
    // the next IRsSync sends every one. `ret`.
    constexpr uintptr_t kIRsForceUpdate = 0x00685A70;
    using IRsForceUpdateFn = void(__fastcall*)(void* device, void* edx);

    // CGxDevice::IRsSync (this, int force), `ret 4`: walks the dirty list and calls slot 1 for each
    // state whose app value differs from the shadow; force != 0 runs IRsForceUpdate first.
    constexpr uintptr_t kIRsSync = 0x00685B50;
    using IRsSyncFn = void(__fastcall*)(void* device, void* edx, int force);

    // CGxDevice::InitLights (this): the four lights' default enables/ranges. `ret`.
    constexpr uintptr_t kInitLights = 0x00682C50;
    using InitLightsFn = void(__fastcall*)(void* device, void* edx);

    // CGxDevice::ShaderConstantsClear (): both constant shadows to FLT_MAX and both dirty ranges to
    // "everything", so every constant is re-sent once. No `this`, no arguments, `ret`. D3D runs it at
    // the top of every frame (ISceneBegin 0x006A3350).
    constexpr uintptr_t kShaderConstantsClear = 0x006833A0;
    using ShaderConstantsClearFn = void(__cdecl*)();

    // CGxDevice::DeviceScreenShot (this): sizes the shot from curWindow and calls slot 20 into the
    // device's own array at kScreenshotWidth + 8. `ret`. D3D ScenePresent (0x006A3450) calls it when
    // kScreenshotPending was set at entry.
    constexpr uintptr_t kDeviceScreenShot = 0x006841D0;
    using DeviceScreenShotFn = void(__fastcall*)(void* device, void* edx);

    // CGxDevice::ClampRectToWindow (this, CiRect*), `ret 4`: clips the rect to curWindow in place.
    // Both read-back slots start with it.
    constexpr uintptr_t kClampRectToWindow = 0x00683CE0;
    using ClampRectToWindowFn = void(__fastcall*)(void* device, void* edx, int32_t* rect);

    // TSGrowableArray<CImVector>::SetCount (this, count), `ret 4`: grows (zero-filling) and sets the
    // count. What D3D DeviceReadPixels (0x0068FED0) sizes its output with. The array is
    // {capacity, count, data, chunk}.
    constexpr uintptr_t kCImVectorArraySetCount = 0x00616CA0;
    using CImVectorArraySetCountFn = void(__fastcall*)(void* array, void* edx, uint32_t count);
    constexpr size_t kGrowableCount = 0x04;
    constexpr size_t kGrowableData  = 0x08;

    // EmergencyMem::Lock (this, size), `ret 4`: the scratch block a buffer lock returns when the
    // backend has nowhere to put it. One per pool type at kEmergencyLock + type * kEmergencyLockStride.
    constexpr uintptr_t kEmergencyMemLock   = 0x00685E90;
    constexpr size_t    kEmergencyLockStride = 0x14;
    using EmergencyMemLockFn = void*(__fastcall*)(void* emergency, void* edx, uint32_t size);

    // CGxPool::Invalidate (pool) 0x00688230 and CGxPool::Discard (pool) 0x00688260, __fastcall with
    // the pool in ECX, plain `ret` (read from the bytes): both clear the valid byte (+0x1C) of every
    // CGxBuf on the pool's list; Discard also rewinds the stream cursor (kPoolStreamCursor) to 0.
    // D3D's IBufLock calls Discard when a stream lock wraps, IReleaseD3dPool calls Invalidate
    // (decomp, read whole).
    constexpr uintptr_t kPoolInvalidate = 0x00688230;
    constexpr uintptr_t kPoolDiscard    = 0x00688260;
    using PoolFn = void(__fastcall*)(void* pool);

    // CGxDevice::ITexWHDStartEnd (this, tex, &w, &h, &start, &end) 0x006A5EF0, __thiscall,
    // `ret 0x14` (read from the bytes): the size of the API texture's level 0 (the CGxTex size >>
    // start), the first chain level the device keeps (kBaseMipLevel, clamped) and one past the
    // last; one level when the filter has no mip mode or GenerateMips is set without SingleMip,
    // `start + 1` with SingleMip. D3D's ITexCreate and ITexUpload both start with it (cgxdevice 4.5).
    constexpr uintptr_t kITexWHDStartEnd = 0x006A5EF0;
    using ITexWHDStartEndFn = void(__fastcall*)(void* device, void* edx, void* tex, uint32_t* width,
                                                uint32_t* height, uint32_t* start, uint32_t* end);

    // GxTexCreate (w, h, format, flags, userArg, fill, CGxTex** out), __cdecl: the 2D wrapper that
    // checks the caps block and calls slot 57. GxTexUpdate (tex, minX, minY, maxX, maxY, immediate),
    // __cdecl: TexMarkForUpdate, which reaches slot 0 when immediate.
    constexpr uintptr_t kGxTexCreate = 0x00681CB0;
    using GxTexCreateFn = int(__cdecl*)(uint32_t width, uint32_t height, uint32_t format, uint32_t flags,
                                        void* userArg, TexFillFn fill, void** out);
    constexpr uintptr_t kGxTexUpdate = 0x00681F20;
    using GxTexUpdateFn = void(__cdecl*)(void* tex, int32_t a, int32_t b, int32_t c, int32_t d, int immediate);

    // The fill callback of ICreateD3dDevice's 8x8 placeholder (FUN_0068F370): cmd 0 fills a static
    // 64-dword block with userArg, cmd 1 hands it out. D3D creates it with userArg 0xFF00FF00.
    constexpr uintptr_t kPlaceholderTexFill = 0x0068F370;
    constexpr uint32_t  kPlaceholderTexColor = 0xFF00FF00;

    // CGxDevice::s_uiVertexShader (CGxShader*[2], ShaderCreate(..., "UI", 2)) and s_uiPixelShader
    // (CGxShader*), created at the end of ICreateD3dDevice and released by IDestroyD3d through slot 69.
    // kUiShaderName is the "UI" literal both ShaderCreate calls pass (.rdata, read from the bytes).
    constexpr uintptr_t kUiVertexShader = 0x00C5DFD8;
    constexpr uintptr_t kUiPixelShader  = 0x00C5FFFC;
    constexpr uintptr_t kUiShaderName   = 0x009E3034;
}
