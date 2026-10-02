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

    // --- blend-state factor tables ---
    // Two parallel arrays of API blend factors, one entry per blend state, read by the single site that
    // pushes blend state to the hardware -- one instruction per table, each with the table's address as
    // an absolute displacement. That makes the pair RELOCATABLE: copy them somewhere larger, patch the
    // two displacements, and the engine reads the wider tables without knowing it. Which is how a blend
    // state the stock table has no room for gets one.
    constexpr uintptr_t kBlendSrcFactors  = 0x00A2F964;
    constexpr uintptr_t kBlendDstFactors  = 0x00A2F994;
    constexpr uint32_t  kBlendStateCount  = 12;       // entries in the stock tables
    constexpr uintptr_t kBlendSrcReadDisp = 0x006A4D94; // displacement operand naming the src table
    constexpr uintptr_t kBlendDstReadDisp = 0x006A4DBD; // ... and the dst table

    // API blend factors as the tables spell them.
    enum : uint32_t
    {
        kD3dBlendZero        = 1,
        kD3dBlendOne         = 2,
        kD3dBlendSrcColor    = 3,
        kD3dBlendInvSrcColor = 4,
        kD3dBlendSrcAlpha    = 5,
        kD3dBlendInvSrcAlpha = 6,
        kD3dBlendDestColor   = 9,
        kD3dBlendInvDestColor = 10,
    };

    // States added past the stock count. The numbering continues the stock enum because the modern
    // asset data numbers them that way -- these are the values a modern particle record already names,
    // not identifiers of our invention.
    constexpr uint32_t kBlendStateScreenAdd   = 12;   // inverse-dest-colour / one
    constexpr uint32_t kBlendStatePremulAlpha = 13;   // one / inverse-src-alpha

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
    // Master switches, one bit per rendering category, read by the device's master-enable check. Bit 8
    // off is what makes the world scene clear to opaque black instead of the horizon colour.
    constexpr size_t    kMasterEnableField = 0x2758;
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
    constexpr unsigned  kGxSetViewSlot         = 0xA4 / 4;   // = 41, device view-upload slot
    using GxSetProjectionFn = void(__fastcall*)(void* self, void* edx, const void* proj16);

    // Where the device keeps the matrices those two slots upload, so they can be read back and
    // restored. The per-frame world render saves both before drawing the world and uploads them again
    // after, then refreshes the shader system -- the world render overwrites them and does not restore
    // them, so anything calling it outside that wrapper has to bracket it the same way.
    //
    // The view is the top of a stack: the live index sits at kDeviceViewIndex and selects a slot of
    // kDeviceViewStride bytes from kDeviceViewBase.
    constexpr size_t    kDeviceProjection = 0x3E2 * 4;
    constexpr size_t    kDeviceViewIndex  = 0x6BE * 4;
    constexpr size_t    kDeviceViewBase   = 0x6C0 * 4;
    constexpr size_t    kDeviceViewStride = 0x10 * 4;

    // Re-derives the shader system's projection from the device. Called after the matrices are put
    // back, since the shaders hold their own copy.
    constexpr uintptr_t kShaderUpdateProjMatrix = 0x00872C10;
    using ShaderUpdateProjMatrixFn = void(__cdecl*)();

    // The GLUE 3D-scene render callback -- the login / character-select model preview.
    // It is the glue-side analogue of the per-frame world render (kWorldRenderFinalize): the engine defers every
    // 3D render into a per-frame-object callback (there is no single global "3D done" point), so the world hook
    // covers the world and THIS hook covers the glue model. Hooked at its entry and the post-process boundary is
    // fired after the original returns (model rendered, glue UI not yet drawn). In-world it also runs for 3D UI
    // portraits, but the world hook already claimed the boundary by then, so the shared latch makes it a no-op.
    constexpr uintptr_t kSimpleModelFFXRender = 0x004E6190;
    using GlueModelRenderFn = void(__cdecl*)(void* frame);

    // The glue model frame's script-method registration, the callback the metatable builder invokes.
    // Detour it, let the stock methods register, then append: the frame types built on it -- including
    // the glue screens' ModelFFX -- inherit whatever was added.
    constexpr uintptr_t kSimpleModelRegisterMethods = 0x009603D0;
    using RegisterScriptMethodsFn = void(__cdecl*)(void* target);

    // The glue model frame's script type-id slot, zero until its first script method claims one from the
    // global counter (engine::lua::kObjectTypeCounter). Resolving the invoked object goes through this id.
    constexpr uintptr_t kSimpleModelTypeId = 0x00DCE428;

    // Scene clear (flags, colour), forwarded to the device's own clear routine: flags bit 0 clears the
    // colour target, bit 1 the depth buffer, so 3 clears both and 2 leaves the colour standing.
    //
    // Every glue 3D object's render opens with an unconditional colour+depth clear -- unconditional in
    // the literal sense that only the model draw between them is guarded, not the clears. Anything
    // painted into the frame beforehand is erased whatever that object turns out to render.
    constexpr uintptr_t kGxSceneClear = 0x006813B0;
    constexpr uint32_t  kSceneClearColor = 1;
    constexpr uint32_t  kSceneClearDepth = 2;
    using GxSceneClearFn = void(__cdecl*)(uint32_t flags, uint32_t colour);

    // M2 triangle-batch draw (this-in-ECX). The hook reads the current model so the per-draw event
    // can name which model is rendering.
    constexpr uintptr_t kDrawTriangleBatch      = 0x008203B0;
    // Draw context -> the current INSTANCE, not the shared model: the draw reads its bone palette at
    // +0x98 and reaches the file header through +0x2C -> +0x150. Anything that wants the model (a path,
    // a registry lookup keyed on shared data) has to take that +0x2C step first; treating this pointer
    // as the model reads whatever the instance happens to hold at the model's field offsets.
    constexpr size_t    kDrawBatchCtxModelField = 0x60;
    // draw context -> copied M2SkinSection. TRAP: both kDrawTriangleBatch and kDrawBatchDoodad refresh
    // this field from the current batch record's own section (kM2ElementSectionField below) as the
    // VERY FIRST thing they do, so a hook hung on either function's entry still sees the PREVIOUS
    // batch's section here -- the draw context object is reused across every batch in a pass, so this
    // is stale on every single call, never just occasionally. A caller that needs the section for the
    // batch about to run (not the one that already ran) must read it fresh off the batch record itself
    // via kDrawBatchCtxElementField + kM2ElementSectionField instead of this field.
    constexpr size_t    kDrawBatchCtxSectionField = 0x90;
    // The batched-doodad sibling of kDrawTriangleBatch above -- __thiscall(this, elements, indices),
    // ret 8, called from the same per-frame sorted batch walk. Shares the identical draw-context shape
    // (kDrawBatchCtxElementField/kDrawBatchCtxSectionField are the same fields this draw entry itself
    // reads before negotiating a co-instance batch), so DrawBatchContext below describes both entries.
    constexpr uintptr_t kDrawBatchDoodad          = 0x00820AE0;
    constexpr size_t    kDrawBatchCtxElementField = 0x50; // draw context -> current M2Element/batch record
    // M2Element/batch-record (what kDrawBatchCtxElementField points at) -> its own, always-current
    // M2SkinSection*. The value kDrawTriangleBatch/kDrawBatchDoodad copy into kDrawBatchCtxSectionField
    // on entry -- read it from here directly to see the batch about to run instead of the stale copy.
    constexpr size_t    kM2ElementSectionField = 0x2C;
    // M2Element/batch-record -> total requested co-instance count for this run. Read once by
    // kDrawBatchDoodad's own entry (its total-work bound) and read AGAIN by CM2SceneRender::Draw's
    // per-batch dispatch loop right after the call returns, to advance its sorted-index cursor past the
    // whole run -- a caller that shrinks this field to issue several smaller native calls (each within
    // the c31-based VS-constant budget) MUST restore it to the original value before returning, or
    // Draw's cursor undershoots and re-visits the run's tail as a bogus second batch. Only the head
    // M2Element of a run of >=2 same-batch-key elements carries a meaningful value here; every other
    // element in the run has it unset, which is exactly why the restore is mandatory, not optional.
    constexpr size_t    kM2ElementRunLengthField = 0x1C;

    // The device-level draw every batch funnels through, one step below the vtable slot an extension
    // may own: __thiscall(this, batch, indexed), ret 8. It reads the CGxBatch the M2 draw built and
    // calls the indexed or non-indexed vtable entry.
    //
    // Worth owning rather than the vtable slot when what has to change is a value the batch carries:
    // CGxBatch::startIndex is 32 bits here, while the M2 draw that filled it read the submesh's start
    // through a 16-bit field. This is the first place on the path where the full value fits.
    constexpr uintptr_t kGxDeviceDraw = 0x006A3620;
    using GxDeviceDrawFn = void(__fastcall*)(void* device, void* edx, uint32_t* batch, int indexed);
    /// The draw descriptor the entry above consumes: 0x10 bytes, built on the M2 draw's own stack.
    constexpr size_t kGxBatchPrimType   = 0x00; // uint32
    constexpr size_t kGxBatchStartIndex = 0x04; // uint32 -- the one wide field on the path
    constexpr size_t kGxBatchIndexCount = 0x08; // uint32
    constexpr size_t kGxBatchMinIndex   = 0x0C; // uint16
    constexpr size_t kGxBatchMaxIndex   = 0x0E; // uint16
    constexpr size_t kGxDeviceVertexStream = 0x2870; // -> the bound vertex stream buffer
    constexpr size_t kGxBufStreamOffset    = 0x18;   // uint32, in bytes
    constexpr size_t kGxBufStreamStride    = 0x0C;   // uint32
    /// Non-zero selects a path that passes a base vertex of zero instead of deriving one, so the
    /// stored offset is ignored there. Anything relying on that derivation has to check this first.
    constexpr size_t kGxDeviceBaseVertexMode = 0x224;

    // --- typed views over the device objects ---
    // The constants above are the curated landmarks; these structs give named, typed access to the same
    // fields, with every member offset checked against a constant at compile time. Only confirmed fields
    // are named; the gaps are explicit padding. Pointers are 4 bytes on the 32-bit client. The graphics-device
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

    /** @brief M2 triangle-batch draw context (this-in-ECX at kDrawTriangleBatch or kDrawBatchDoodad). */
    struct DrawBatchContext
    {
        uint8_t  _pad00[kDrawBatchCtxElementField];
        void*    element;          // kDrawBatchCtxElementField -> current M2Element/batch record
        uint8_t  _pad54[kDrawBatchCtxModelField - (kDrawBatchCtxElementField + sizeof(void*))];
        void*    model;            // kDrawBatchCtxModelField -> current INSTANCE (see the note there)
        uint8_t  _pad64[kDrawBatchCtxSectionField - (kDrawBatchCtxModelField + sizeof(void*))];
        void*    section;          // kDrawBatchCtxSectionField -> copied M2SkinSection for this draw
    };
    static_assert(offsetof(DrawBatchContext, element) == kDrawBatchCtxElementField, "DrawBatchContext.element");
    static_assert(offsetof(DrawBatchContext, model) == kDrawBatchCtxModelField, "DrawBatchContext.model");
    static_assert(offsetof(DrawBatchContext, section) == kDrawBatchCtxSectionField, "DrawBatchContext.section");
#pragma pack(pop)

    // World-frame finalize render callback, once per frame. Hook its entry and fire the event after the
    // original returns: world done, UI not yet started. The world -> UI boundary / post-fx slot. The
    // epilogue-anchor address is mid-epilogue, not a hookable entry; kept only as a landmark.
    // The world render pass alone: viewport, scene draw, and the passes around it. Its caller
    // (kWorldRenderFinalize) pairs it with the world frame's own per-frame update, which is a different
    // kind of work -- free lists, effect managers, pending portraits, all belonging to a world frame the engine
    // built. A scene drawn for a frame the engine did not build wants this half and not that one.
    constexpr uintptr_t kWorldOnRender = 0x004F8EA0;
    using WorldOnRenderFn = void(__fastcall*)(void* worldFrame, void* edx);

    constexpr uintptr_t kWorldRenderFinalize = 0x004FAF90;
    constexpr uintptr_t kWorldRenderEpilogueAnchor = 0x004FB074; // landmark only, do NOT hook
    using WorldRenderFinalizeFn = void(__cdecl*)(void* worldFrame);

    // Sets a texture object's wrap mode (tex, wrapU, wrapV): 1 = repeat, 0 = clamp. The WMO batch draws
    // derive both flags from the material record before binding stage 0.
    constexpr uintptr_t kGxTexSetWrap = 0x00681450;
    using GxTexSetWrapFn = void(__cdecl*)(void* gxTex, int wrapU, int wrapV);

    // Central texture-data upload to the device (deviceTex, x, y, x2, y2, flag). Full-surface uploads
    // pass (tex, 0, 0, width, height, 1), so width = x2 - x and height = y2 - y.
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
    // The table is filled per build (the mip-chain lock writes kMipTablePtr[mip] = alias) only for mip
    // levels with a nonzero size, so a truncated mip chain (common in custom-map BLPs) under-fills it and
    // leaves a previous build's freed alias in a high slot. The upload walks mips by header dimensions and
    // would read that stale slot and fault (a use-after-free on a cold custom-map login). Its per-mip blit
    // is guarded by "source != 0", so clearing the table after each upload makes an under-filled build's
    // high slots read 0 and get skipped.

    // The buffer behind kMipTablePtr is allocated ONCE at boot sized for a 32-bpp
    // 0x400 x 0x400 mip chain (~5.59 MB), and every synchronous mip fill (atlas reload, self-heal, TGA)
    // memcpys the texture's decoded chain into it. A 2048 DXT5 chain already
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
        constexpr unsigned kSetTransform           = 44;
        constexpr unsigned kGetTransform           = 45;
        constexpr unsigned kSetViewport            = 47;
        constexpr unsigned kGetViewport            = 48;
        constexpr unsigned kSetRenderState         = 57;
        constexpr unsigned kGetRenderState         = 58;
        constexpr unsigned kGetTexture             = 64;
        constexpr unsigned kSetTexture             = 65;
        constexpr unsigned kGetTextureStageState   = 66;
        constexpr unsigned kSetTextureStageState   = 67;
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
        constexpr unsigned kGetPixelShaderConstantF = 110;
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
    constexpr unsigned  kVsConstRegisters = 256;        // registers in the file the cache mirrors
    // Where the M2 skinning palette lands in that file. The upload transposes each bone's upper 3x4
    // into three consecutive vec4s, so bone N owns registers [kVsBonePaletteBase + N*kVsBonePaletteRegs,
    // + kVsBonePaletteRegs) and its translation is the fourth component of each of the three rather
    // than a row of its own. The palette runs to the end of the file, which is what fixes the per-draw
    // bone ceiling at (kVsConstRegisters - kVsBonePaletteBase) / kVsBonePaletteRegs.
    constexpr unsigned  kVsBonePaletteBase = 31;
    constexpr unsigned  kVsBonePaletteRegs = 3;
    // The PIXEL half of the same structure, and the reason a constant written for one draw is still
    // there for the next: both caches are process-wide, not per-shader. The setter (0x0069E970,
    // reached through the device vtable at +0x118) forks on its target argument -- 0 takes the vertex
    // arrays above, anything else takes these. ShaderConstantsClear (0x006833A0) clears both as
    // 0x400 dwords each, which is what fixes them at 256 registers and makes the two halves
    // contiguous: cache, dirty-end, dirty-start, then the next cache.
    constexpr uintptr_t kPsConstCache     = 0x00C5DFE0; // float[256*4], CGxDevice::s_shadowConstants
    constexpr uintptr_t kPsDirtyRegStart  = 0x00C5EFE4; // uint32: lowest dirty register (0xFF = none)
    constexpr uintptr_t kPsDirtyRegEnd    = 0x00C5EFE0; // uint32: highest dirty register (0 = none)
}
