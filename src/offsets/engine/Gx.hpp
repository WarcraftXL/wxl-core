// Graphics device addresses, D3D9 vtable indices, and render-state ids.
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

// INTERNAL to the core. The graphics device and the render-pipeline hook points the gx bindings and
// the render events are built from. Modules never include this; they use wxl::game::gx / wxl::events.
namespace wxl::offsets::engine::gx
{
    // The graphics device singleton; the live D3D9 device sits at +kD3DDeviceField inside it.
    constexpr uintptr_t kGxDevicePtr    = 0x00C5DF88; // -> graphics device object
    constexpr size_t    kD3DDeviceField = 0x397C;     // graphics device -> IDirect3DDevice9*

    // Cached render-target surfaces on the graphics-device object.
    constexpr size_t    kBackBufferField   = 0x3B3C; // cached back-buffer surface
    constexpr size_t    kDepthSurfaceField = 0x3B40; // cached world depth surface

    // Render-resolution rects on the graphics-device object. Each is a rect of 4 floats
    // {minX,minY,maxX,maxY}; min stays 0, so maxX/maxY hold the pixel width/height. curWindow is the live
    // render resolution every normalized [0..1] viewport is multiplied by at draw time; defWindow is its
    // source (curWindow = defWindow whenever the backbuffer is bound). The active format holds the backbuffer
    // pixel size. Supersampling scales curWindow + defWindow to format*S (the proxy enlarges the matching
    // backbuffer), so the engine renders the full world+UI at the higher resolution. Width is read from
    // rect+0xC and height from rect+0x8, and defWindow is copied whole into curWindow on every backbuffer
    // bind, so both rects use the same width-at-+0xC / height-at-+0x8 convention.
    constexpr size_t    kDefWindowWidth  = 0x170; // defWindow + 0xC
    constexpr size_t    kDefWindowHeight = 0x16C; // defWindow + 0x8
    constexpr size_t    kCurWindowWidth  = 0x180; // curWindow + 0xC = live render width
    constexpr size_t    kCurWindowHeight = 0x17C; // curWindow + 0x8 = live render height
    constexpr size_t    kFormatWidth     = 0x1D0; // active format width  (backbuffer px, unscaled)
    constexpr size_t    kFormatHeight    = 0x1D4; // active format height (backbuffer px, unscaled)
    constexpr size_t    kViewportDirty   = 0xF6C; // set to 1 to force a viewport recompute from curWindow
    constexpr size_t    kRtOverrideField = 0x2918; // non-zero while an offscreen RT override is active (not the backbuffer pass)
    constexpr uintptr_t kDeviceSetDefWindow = 0x00684360; // resolution choke (create + every resize)

    // Engine render-target bind chokepoint: the world (and UI) bind their target through this method, which
    // bypasses the D3D9 device vtable, so the supersampling redirect must also hook it here.
    constexpr uintptr_t kGxDeviceVTable        = 0x00A2E718; // engine graphics-device vtable base
    constexpr unsigned  kGxSetRenderTargetSlot = 23;         // SetRenderTarget(slot, rt, face)

    // Engine projection upload (device vtable slot, byte 0xA0). The camera setup uploads its perspective
    // projection -- a row-major float[16] in the SAME layout as the world camera projection global 0x00ADF628
    // (same builder): index 0 = X scale, 5 = Y scale, 10 = z scale, 11 = 1.0 (the w=z term), 14 = z bias --
    // straight through this slot. Hooked as an observer to snapshot the GLUE scene's projection: the glue 3D
    // camera is not written to the world camera globals (they stay identity on the glue screens), so a
    // depth-using effect (ambient occlusion) on the glue screens has no matrix without capturing it here.
    constexpr unsigned  kGxSetProjectionSlot   = 0xA0 / 4;   // = 40, device projection-upload slot
    using GxSetProjectionFn = void(__fastcall*)(void* self, void* edx, const void* proj16);

    // CSimpleModelFFX::Render: the GLUE 3D-scene render callback -- the login / character-select model preview.
    // It is the glue-side analogue of CGWorldFrame::RenderWorld (kWorldRenderFinalize): the engine defers every
    // 3D render into a per-frame-object callback (there is no single global "3D done" point), so the world hook
    // covers the world and THIS hook covers the glue model. Hooked at its entry and the post-process boundary is
    // fired after the original returns (model rendered, glue UI not yet drawn). In-world it also runs for 3D UI
    // portraits, but the world hook already claimed the boundary by then, so the shared latch makes it a no-op.
    constexpr uintptr_t kSimpleModelFFXRender = 0x004E6190;
    using GlueModelRenderFn = void(__cdecl*)(void* frame);

    // M2 triangle-batch draw (this-in-ECX). The hook reads the current model so the per-draw event
    // can name which model is rendering.
    constexpr uintptr_t kDrawTriangleBatch      = 0x008203B0;
    constexpr size_t    kDrawBatchCtxModelField = 0x60; // draw context -> current model
    constexpr size_t    kDrawBatchCtxSectionField = 0x90; // draw context -> copied M2SkinSection

    // --- typed views over the device objects ---
    // The constants above are the curated landmarks; these structs give named, typed access to the same
    // fields, with every member offset checked against a constant at compile time. Only RE'd fields are
    // named; the gaps are explicit padding. Pointers are 4 bytes on the 32-bit client. The graphics-device
    // singleton pointer, the vtable indices, the function addresses, and the render-state ids stay as plain
    // constants: they are not struct fields.
#pragma pack(push, 1)
    /** @brief Graphics-device object (the kGxDevicePtr target): the live D3D device and cached surfaces. */
    struct GxDevice
    {
        uint8_t  _pad0000[kD3DDeviceField];
        void*    d3dDevice;        // kD3DDeviceField -> IDirect3DDevice9*
        uint8_t  _pad3980[kBackBufferField - (kD3DDeviceField + sizeof(void*))];
        void*    backBuffer;       // kBackBufferField (cached back-buffer surface)
        void*    depthSurface;     // kDepthSurfaceField (cached world depth surface)
    };
    static_assert(offsetof(GxDevice, d3dDevice)    == kD3DDeviceField,   "GxDevice.d3dDevice");
    static_assert(offsetof(GxDevice, backBuffer)   == kBackBufferField,  "GxDevice.backBuffer");
    static_assert(offsetof(GxDevice, depthSurface) == kDepthSurfaceField, "GxDevice.depthSurface");

    /** @brief M2 triangle-batch draw context (this-in-ECX at kDrawTriangleBatch). */
    struct DrawBatchContext
    {
        uint8_t  _pad00[kDrawBatchCtxModelField];
        void*    model;            // kDrawBatchCtxModelField -> current model
        uint8_t  _pad64[kDrawBatchCtxSectionField - (kDrawBatchCtxModelField + sizeof(void*))];
        void*    section;          // kDrawBatchCtxSectionField -> copied M2SkinSection for this draw
    };
    static_assert(offsetof(DrawBatchContext, model) == kDrawBatchCtxModelField, "DrawBatchContext.model");
    static_assert(offsetof(DrawBatchContext, section) == kDrawBatchCtxSectionField, "DrawBatchContext.section");
#pragma pack(pop)

    // World-frame finalize render callback, once per frame. Hook its entry and fire the event after the
    // original returns: world done, UI not yet started. The world -> UI boundary / post-fx slot. The
    // epilogue-anchor address is mid-epilogue, not a hookable entry; kept only as a landmark.
    constexpr uintptr_t kWorldRenderFinalize = 0x004FAF90;
    constexpr uintptr_t kWorldRenderEpilogueAnchor = 0x004FB074; // landmark only, do NOT hook
    using WorldRenderFinalizeFn = void(__cdecl*)(void* worldFrame);

    // Central texture-data upload to the device (deviceTex, x, y, x2, y2, flag). Full-surface uploads pass
    // (tex, 0, 0, width, height, 1), so width = x2 - x, height = y2 - y. The single __cdecl choke point all
    // upload paths funnel through.
    constexpr uintptr_t kTextureUpdate = 0x00681F20;
    using TextureUpdateFn = void(__cdecl*)(void* deviceTex, int x, int y, int x2, int y2, int flag);

    // Central by-name texture create API (__cdecl). The single choke point all texture requests funnel
    // through; fires on every reference (returns the cached handle on a hit), so it sees the name of each
    // BLP requested. fileName is the full null-terminated virtual path (e.g. "World\...\foo.blp"); match
    // case-insensitively, slash-normalized. The returned texture handle carries the same name at
    // kTexHandleNameField. flags/flags2 control load options and are not needed to identify the texture.
    constexpr uintptr_t kTextureCreate      = 0x004B9760;
    constexpr size_t    kTexHandleNameField = 0x6C; // texture handle -> stored name (capped 0x104)
    using TextureCreateFn = void*(__cdecl*)(const char* fileName, uint32_t flags, int* status, uint32_t flags2);

    // Process-wide singleton the BLP decode writes per build (mip pointer table at the head of the buffer
    // it points to) and the upload reads. Not reentrancy-safe: a nested build during a force-wait rewrites
    // it under the outer build. kMipTablePtr holds the buffer pointer; the table is at *kMipTablePtr.
    constexpr uintptr_t kMipTablePtr   = 0x00B49C90;
    constexpr uintptr_t kMipTableValid = 0x00B49C94; // nonzero while the table is live (gates a reload)
    constexpr size_t    kMipTableSlots = 16;         // upper bound on mip levels (real count <= 13)
    // The table is filled per build (CBLPFile_LockChain2 writes kMipTablePtr[mip] = alias) only for mip
    // levels with a nonzero size, so a truncated mip chain (common in custom-map BLPs) under-fills it and
    // leaves a previous build's freed alias in a high slot. The upload (CGxDeviceD3d__ITexUpload) walks mips
    // by header dimensions and would read that stale slot and fault (the 0x40cb6a UAF on a cold custom-map
    // login). Its per-mip blit is guarded by "source != 0", so clearing the table after each upload makes an
    // under-filled build's high slots read 0 and get skipped.

    // The buffer behind kMipTablePtr is allocated ONCE at boot (TextureInitialize) sized for a 32-bpp
    // 0x400 x 0x400 mip chain (~5.59 MB), and every synchronous mip fill (CBLPFile_LockChain: atlas
    // reload, self-heal, TGA) memcpys the texture's decoded chain into it. A 2048 DXT5 chain already
    // exceeds that capacity by a few bytes, so any texture wider than 1024 corrupts the heap the first
    // time it takes the sync path. The two addresses below are the width/height push imm32 operands of
    // the boot-time size computation; widening both to 0x800 (32-bpp 2048 chain, ~22.4 MB) makes every
    // chain of any encoding up to 2048 fit.
    constexpr uintptr_t kMipScratchDimHImm = 0x004B7F8D; // push imm32 operand, height arg
    constexpr uintptr_t kMipScratchDimWImm = 0x004B7F92; // push imm32 operand, width arg
    constexpr uint32_t  kMipScratchStockEdge = 0x400;    // shipped operand value at both sites
    constexpr uint32_t  kMipScratchWideEdge  = 0x800;    // widened capacity (2048 any-encoding chains)

    // Per-frame liquid render pass loop (this-in-ECX). Brackets every visible liquid instance of one pass;
    // both passes route through it (passType 0 main, 1 secondary). Runs late in the frame, after the liquid
    // textures are bound and the render queues flush, so the wave/ripple animation is already applied.
    // ECX is the liquid material-settings bank: an array of LiquidPassEntry indexed by passType.
    constexpr uintptr_t kLiquidRenderPass = 0x008A2240;
    using LiquidRenderPassFn = void(__fastcall*)(void* bank, void* edx, void* transform, int passType);

#pragma pack(push, 1)
    /** @brief One entry of the liquid material-settings bank (kLiquidRenderPass ECX, indexed by passType).
     *  instances is a 4-byte client pointer; kept as a u32 so the layout is host-width independent. */
    struct LiquidPassEntry
    {
        uint32_t _unk00;    // 0x00
        uint32_t count;     // 0x04 visible instance count for this pass
        uint32_t instances; // 0x08 -> instance array (4-byte client pointer)
        uint32_t _unk0c;    // 0x0C
    };
    static_assert(sizeof(LiquidPassEntry) == 0x10, "LiquidPassEntry");
    static_assert(offsetof(LiquidPassEntry, count)     == 0x04, "LiquidPassEntry.count");
    static_assert(offsetof(LiquidPassEntry, instances) == 0x08, "LiquidPassEntry.instances");
#pragma pack(pop)

    // IDirect3DDevice9 vtable indices used by the gx facade.
    namespace vt
    {
        constexpr unsigned kRelease                = 2;  // COM / shader object release
        constexpr unsigned kReset                  = 16; // IDirect3DDevice9::Reset (resolution / window resize)
        constexpr unsigned kPresent                = 17; // IDirect3DDevice9::Present (per-frame flip)
        constexpr unsigned kGetBackBuffer          = 18;
        constexpr unsigned kCreateTexture          = 23;
        constexpr unsigned kStretchRect            = 34;
        constexpr unsigned kSetRenderTarget        = 37;
        constexpr unsigned kGetRenderTarget        = 38;
        constexpr unsigned kSetDepthStencil        = 39;
        constexpr unsigned kGetDepthStencil        = 40;
        constexpr unsigned kBeginScene             = 41;
        constexpr unsigned kEndScene               = 42;
        constexpr unsigned kClear                  = 43;
        constexpr unsigned kSetViewport            = 47;
        constexpr unsigned kGetViewport            = 48;
        constexpr unsigned kSetRenderState         = 57;
        constexpr unsigned kGetRenderState         = 58;
        constexpr unsigned kSetTexture             = 65;
        constexpr unsigned kGetTexture             = 66;
        constexpr unsigned kSetSamplerState        = 69;
        constexpr unsigned kDrawPrimitiveUP        = 83;
        constexpr unsigned kSetFVF                 = 89;
        constexpr unsigned kCreateVertexShader     = 91;
        constexpr unsigned kSetVertexShader        = 92;
        constexpr unsigned kGetVertexShader        = 93;
        constexpr unsigned kSetVertexShaderConstantF = 94;
        constexpr unsigned kGetVertexShaderConstantF = 95;
        constexpr unsigned kSetStreamSource        = 100;
        constexpr unsigned kGetStreamSource        = 101;
        constexpr unsigned kCreatePixelShader      = 106;
        constexpr unsigned kSetPixelShader         = 107;
        constexpr unsigned kGetPixelShader         = 108;
        constexpr unsigned kSetPixelShaderConstantF = 109;
        constexpr unsigned kDrawIndexedPrimitive   = 0x148 / 4;
    }

    // Engine-internal shader-constant upload (the device's own constant path), addressed as a vtable
    // byte-offset on the graphics-device object. shaderType 0 = vertex, 4 = pixel.
    constexpr int kVtSetShaderConstant = 0x118 / 4; // byte 0x118

    // Engine-internal shader-constant uploader: native this-in-ECX; declared with a dummy second
    // parameter so the trampoline keeps the trailing arguments on the stack.
    using Gx_SetShaderConstantFn = void(__fastcall*)(void* device, void* edx, uint32_t shaderType, uint32_t startReg, const float* data, uint32_t vec4Count);

    // VS float-constant cache: 256-register software buffer (4 floats per register) that the
    // kVtSetShaderConstant path writes; the pre-draw flush uploads only the dirty range to the device.
    // The setter skips marking a register dirty when the incoming value equals the cached value, so a
    // register whose cache entry already matches what we want to write is NOT re-uploaded to the device
    // even if the device was clobbered by a different draw in the meantime.
    constexpr uintptr_t kVsConstCache     = 0x00C5EFE8; // float[256*4]: register N at [N*4] floats
    constexpr uintptr_t kVsDirtyRegStart  = 0x00C5FFEC; // uint32: lowest dirty register (0xFF = none)
    constexpr uintptr_t kVsDirtyRegEnd    = 0x00C5FFE8; // uint32: highest dirty register (0 = none)
}
