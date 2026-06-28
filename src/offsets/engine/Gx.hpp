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

    // Render-resolution rects on the graphics-device object. Each is a GxRect of 4 floats
    // {minX,minY,maxX,maxY}; min stays 0, so maxX/maxY hold the pixel width/height. curWindow is the LIVE
    // render resolution every normalized [0..1] viewport is multiplied by at draw time; defWindow is its
    // source (curWindow = defWindow whenever the backbuffer is bound). The active CGxFormat holds the
    // backbuffer pixel size. Supersampling scales curWindow + defWindow to format*S (the proxy enlarges the
    // matching backbuffer), so the engine renders the full world+UI at the higher resolution.
    // The engine reads render WIDTH from rect+0xC and HEIGHT from rect+0x8 (verified in IXformSetViewport),
    // and copies defWindow -> curWindow whole on every backbuffer bind, so BOTH rects must use the same
    // width-at-+0xC / height-at-+0x8 convention or the copy swaps W/H (renders portrait).
    constexpr size_t    kDefWindowWidth  = 0x170; // defWindow + 0xC
    constexpr size_t    kDefWindowHeight = 0x16C; // defWindow + 0x8
    constexpr size_t    kCurWindowWidth  = 0x180; // curWindow + 0xC = live render width
    constexpr size_t    kCurWindowHeight = 0x17C; // curWindow + 0x8 = live render height
    constexpr size_t    kFormatWidth     = 0x1D0; // active CGxFormat width  (backbuffer px, unscaled)
    constexpr size_t    kFormatHeight    = 0x1D4; // active CGxFormat height (backbuffer px, unscaled)
    constexpr size_t    kViewportDirty   = 0xF6C; // set to 1 to force a viewport recompute from curWindow
    constexpr uintptr_t kDeviceSetDefWindow = 0x00684360; // resolution choke (create + every resize)

    // M2 triangle-batch draw (this-in-ECX). The hook reads the current model so the per-draw event
    // can name which model is rendering.
    constexpr uintptr_t kDrawTriangleBatch      = 0x008203B0;
    constexpr size_t    kDrawBatchCtxModelField = 0x60; // draw context -> current model

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
    };
    static_assert(offsetof(DrawBatchContext, model) == kDrawBatchCtxModelField, "DrawBatchContext.model");
#pragma pack(pop)

    // World-frame finalize render callback (AURENDERCALLBACK), once per frame. Hook its ENTRY and fire the
    // event AFTER the original returns = world done, UI not yet started. The world -> UI boundary / post-fx
    // slot. (The interior address 0x004FB074 is mid-epilogue, NOT a hookable entry; kept only as an anchor.)
    constexpr uintptr_t kWorldRenderFinalize = 0x004FAF90;
    constexpr uintptr_t kWorldRenderEpilogueAnchor = 0x004FB074; // doc anchor only, do NOT hook
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
