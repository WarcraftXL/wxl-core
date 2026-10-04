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
    constexpr size_t kMatrixStackFlags  = 0x108;
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

    constexpr size_t kPoolList          = 0x2760; // TSList of every CGxPool
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
    constexpr size_t kAttribChanged     = 0x28AC;
    constexpr size_t kVertexFormat      = 0x28B0; // EGxVertexBufferFormat, or kVertexFormatCustom
    constexpr size_t kVertexBuf         = 0x28B4; // the CGxBuf of a fixed format
    constexpr size_t kVertexStride      = 0x28B8;
    constexpr size_t kIndexBuf          = 0x28BC;
    constexpr size_t kIndexBufChanged   = 0x28C0;
    constexpr uint32_t kVertexFormatCustom = 14;  // the slots describe the layout instead of a format

    constexpr size_t kEmergencyLock     = 0x28D4; // + pool type * 0x14: a lock that had nowhere to go

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
    constexpr size_t kTexApiObject     = 0x38;
    constexpr size_t kTexApiObject2    = 0x3C;
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
    constexpr size_t kShaderHasCode    = 0x4C;
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
}
