// Render-pipeline detours that publish the render events.
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

#include "runtime/RenderHooks.hpp"

#include "core/Hook.hpp"
#include "core/Logger.hpp"
#include "events/Event.hpp"
#include "game/gx/Gx.hpp"
#include "gpu/Proxy.hpp"
#include "offsets/engine/Gx.hpp"
#include "offsets/game/M2.hpp"
#include "structure/m2/M2Format.hpp"

#include <windows.h>
#include <d3d9.h>

// Uses device-vtable pointer swaps (DrawIndexedPrimitive, EndScene, Present, SetRenderTarget,
// SetDepthStencilSurface) and function-entry hooks; no mid-function inline patch, so the world render
// pass stays intact. The world->UI post-fx slot is served from the render-finalize hook.
namespace
{
    namespace off   = wxl::offsets::engine::gx;
    namespace ev    = wxl::events;
    namespace gx    = wxl::game::gx;
    namespace m2off = wxl::offsets::game::m2;

    // The model currently drawing, captured between a batch-draw enter and its DrawIndexedPrimitive.
    void* g_curModel = nullptr;
    // The current M2 batch draw context; its copied skin-section pointer is populated before the DIP call.
    void* g_curDrawCtx = nullptr;
    // Re-entrancy guard: a subscriber re-issues the draw through the hooked vtable, so do not re-emit.
    bool  g_inM2Emit = false;

    using DrawBatchFn  = void (__fastcall*)(void* ctx, void* edx);
    using DIPFn        = long (__stdcall*)(void*, int, int, unsigned, unsigned, unsigned, unsigned);
    using EndSceneFn   = long (__stdcall*)(void*);
    using PresentFn    = long (__stdcall*)(void*, const void*, const void*, void*, const void*);

    DrawBatchFn  g_origDrawBatch  = nullptr;
    DIPFn        g_origDIP        = nullptr;
    EndSceneFn   g_origEndScene   = nullptr;
    PresentFn    g_origPresent    = nullptr;
    off::WorldRenderFinalizeFn g_origWorldFinalize = nullptr;
    off::LiquidRenderPassFn    g_origLiquidRender  = nullptr;

    // Ribbon multi-texture: set true around a >= 3 layer ribbon's single native pass so the DIP override
    // folds its bound layers into one combine. The native ribbon draw is hooked separately below.
    m2off::M2_RibbonDrawFn g_origRibbonDraw = nullptr;
    bool                   g_ribbonModern   = false;

    /**
     * @brief Detours the M2 batch draw, recording the drawing model so the per-draw event can name it.
     * @param ctx  draw context carrying the model field.
     * @param edx  unused register slot for the thiscall convention.
     */
    void __fastcall hkDrawBatch(void* ctx, void* edx)
    {
        void* prevModel = g_curModel;
        void* prevCtx   = g_curDrawCtx;
        g_curModel = static_cast<off::DrawBatchContext*>(ctx)->model;
        g_curDrawCtx = ctx;
        g_origDrawBatch(ctx, edx);
        g_curDrawCtx = prevCtx;
        g_curModel = prevModel;
    }

    /**
     * @brief Re-expands modern skin section index starts before the D3D draw.
     *
     * The 3.3.5 M2 draw path truncates M2SkinSection::indexStart to 16 bits when passing StartIndex to
     * DrawIndexedPrimitive. Retail character and equipment skins use section.level as the high 16 bits of
     * the index window.
     * By the time DIP is called, drawCtx+0x90 points at the copied M2SkinSection for this batch, so the vtable
     * hook can restore the full 32-bit StartIndex without touching normal legacy sections.
     */
    unsigned ExpandM2StartIndex(unsigned startIndex) noexcept
    {
        if (!g_curDrawCtx) return startIndex;
        __try
        {
            const auto* ctx = static_cast<const off::DrawBatchContext*>(g_curDrawCtx);
            const auto* sec = static_cast<const wxl::structure::m2::M2SkinSection*>(ctx->section);
            if (!sec || sec->level == 0) return startIndex;
            if ((startIndex & 0xFFFFu) != sec->indexStart) return startIndex;
            const unsigned expanded = (static_cast<unsigned>(sec->level) << 16) | sec->indexStart;
            static unsigned logged = 0;
            if (logged < 16)
            {
                ++logged;
                WLOG_INFO("render: expanded M2 StartIndex %u -> %u (section=%u level=%u count=%u)",
                          startIndex, expanded,
                          static_cast<unsigned>(sec->skinSectionId),
                          static_cast<unsigned>(sec->level),
                          static_cast<unsigned>(sec->indexCount));
            }
            return expanded;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return startIndex;
        }
    }

    /**
     * @brief Folds a three-layer ribbon's bound textures into one fixed-function pass.
     *
     * Combines tex0*tex1*tex2*color*4 (MODULATE, MODULATE, MODULATE4X). Stage state is saved and
     * restored so the next draw is unaffected; the additive frame blend the emitter set stays in place.
     * @param dev  D3D9 device.
     * @param pt   primitive type.
     * @param bv   base vertex index.
     * @param mi   minimum vertex index.
     * @param nv   vertex count.
     * @param si   start index.
     * @param pc   primitive count.
     * @return the DrawIndexedPrimitive result.
     */
    long DrawRibbonMultiTexture(IDirect3DDevice9* dev, int pt, int bv, unsigned mi, unsigned nv, unsigned si, unsigned pc)
    {
        DWORD s[4][4];
        for (DWORD st = 0; st < 4; ++st)
        {
            dev->GetTextureStageState(st, D3DTSS_COLOROP,   &s[st][0]);
            dev->GetTextureStageState(st, D3DTSS_COLORARG1, &s[st][1]);
            dev->GetTextureStageState(st, D3DTSS_COLORARG2, &s[st][2]);
            dev->GetTextureStageState(st, D3DTSS_ALPHAOP,   &s[st][3]);
        }

        const D3DTEXTUREOP op[3] = { D3DTOP_MODULATE, D3DTOP_MODULATE, D3DTOP_MODULATE4X };
        for (DWORD st = 0; st < 3; ++st)
        {
            dev->SetTextureStageState(st, D3DTSS_COLOROP,   op[st]);
            dev->SetTextureStageState(st, D3DTSS_COLORARG1, D3DTA_TEXTURE);
            dev->SetTextureStageState(st, D3DTSS_COLORARG2, st == 0 ? D3DTA_DIFFUSE : D3DTA_CURRENT);
            dev->SetTextureStageState(st, D3DTSS_ALPHAOP,   op[st]);
            dev->SetTextureStageState(st, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
            dev->SetTextureStageState(st, D3DTSS_ALPHAARG2, st == 0 ? D3DTA_DIFFUSE : D3DTA_CURRENT);
        }
        dev->SetTextureStageState(3, D3DTSS_COLOROP, D3DTOP_DISABLE);

        long r = g_origDIP(dev, pt, bv, mi, nv, si, pc);

        for (DWORD st = 0; st < 4; ++st)
        {
            dev->SetTextureStageState(st, D3DTSS_COLOROP,   s[st][0]);
            dev->SetTextureStageState(st, D3DTSS_COLORARG1, s[st][1]);
            dev->SetTextureStageState(st, D3DTSS_COLORARG2, s[st][2]);
            dev->SetTextureStageState(st, D3DTSS_ALPHAOP,   s[st][3]);
        }
        return r;
    }

    /**
     * @brief Detours DrawIndexedPrimitive, folding multi-texture ribbons and emitting OnM2BatchDraw.
     *
     * A multi-texture ribbon pass is folded into one combine; otherwise the native draw runs and the
     * M2 batch is published with its draw parameters. Guarded so a subscriber re-issue does not recurse.
     * @param dev  D3D9 device.
     * @param pt   primitive type.
     * @param bv   base vertex index.
     * @param mi   minimum vertex index.
     * @param nv   vertex count.
     * @param si   start index.
     * @param pc   primitive count.
     * @return the DrawIndexedPrimitive result.
     */
    long __stdcall hkDIP(void* dev, int pt, int bv, unsigned mi, unsigned nv, unsigned si, unsigned pc)
    {
        if (g_ribbonModern)
            return DrawRibbonMultiTexture(static_cast<IDirect3DDevice9*>(dev), pt, bv, mi, nv, si, pc);

        const unsigned drawStartIndex = ExpandM2StartIndex(si);
        long r = g_origDIP(dev, pt, bv, mi, nv, drawStartIndex, pc);
        if (g_curModel && !g_inM2Emit)
        {
            g_inM2Emit = true;
            ev::M2BatchDrawArgs a{ dev, g_curModel, pt, bv, mi, nv, drawStartIndex, pc };
            ev::Emit(ev::Event::OnM2BatchDraw, &a);
            g_inM2Emit = false;
        }
        return r;
    }

    /**
     * @brief Detours EndScene, emitting OnEndScene before the native call.
     * @param dev  D3D9 device.
     * @return the EndScene result.
     */
    long __stdcall hkEndScene(void* dev)
    {
        ev::EndSceneArgs a{ dev };
        ev::Emit(ev::Event::OnEndScene, &a);
        return g_origEndScene(dev);
    }

    void SsaaArmFrame();   // defined in the supersampling section below; arms the redirect each frame

    // World -> UI boundary coordination, shared by the world-only hook (hkWorldFinalize) and the general
    // first-UI-quad hook (hkGxDeviceDraw, which also covers the glue screens). OnWorldRenderEnd must fire
    // exactly once per frame: whichever boundary runs first claims it via g_boundaryFired. In-world the world
    // hook always wins (it runs before the UI quads); on the glue screens, where the world hook is silent, the
    // UI-quad hook fires it. g_worldActive records that we are in-world (the world hook fired this frame), so the
    // SSAA/depth redirect arms only there. All three reset at Present.
    bool g_boundaryFired = false;   // OnWorldRenderEnd already emitted this frame (once-per-frame latch)
    bool g_inBoundary    = false;   // re-entrancy guard while emitting the boundary event
    bool g_worldActive   = false;   // the world hook fired this frame (in-world, not a glue screen)
    bool g_inGlueRender  = false;   // true while the glue 3D model renders (gates the projection snapshot)

    /**
     * @brief Detours Present, emitting OnFrame just before the buffers swap.
     * @param dev    D3D9 device.
     * @param src    source rect.
     * @param dst    destination rect.
     * @param wnd    target window override.
     * @param dirty  dirty region.
     * @return the Present result.
     */
    long __stdcall hkPresent(void* dev, const void* src, const void* dst, void* wnd, const void* dirty)
    {
        ev::FrameArgs a{ dev };
        ev::Emit(ev::Event::OnFrame, &a);
        // Arm the supersampling redirect for the next frame: the world renders before the world->UI
        // boundary, so the SetRenderTarget filter must already be armed when the next frame binds its backbuffer.
        // SsaaArmFrame reads g_worldActive (set this frame by the world hook) to gate arming to in-world.
        SsaaArmFrame();
        // Reset the per-frame boundary latches for the coming frame. The next world frame's WorldFinalize re-sets
        // g_worldActive; on a glue frame it stays false so the redirect never arms there.
        g_boundaryFired = false;
        g_worldActive   = false;
        return g_origPresent(dev, src, dst, wnd, dirty);
    }

    // World-only supersampling. During the world pass the color render target is redirected into a larger
    // offscreen surface and the engine viewport is scaled, so the world rasterizes at render size; at the
    // world to UI boundary the offscreen color is downsampled into the backbuffer and the native targets are
    // restored, so the UI draws at native resolution. The redirect is applied at both the engine render-target
    // bind and the D3D9 render-target bind. A depth-using post-process (ambient occlusion) needs a sampleable
    // depth, so the world depth is bound to a shader-readable depth surface that the module reads afterwards.
    using SetRTFn   = long (__stdcall*)(void*, unsigned long, void*);
    using SetDSFn   = long (__stdcall*)(void*, void*);
    using GxSetRTFn = void (__fastcall*)(void*, void*, int, int, int);
    using ResetFn   = long (__stdcall*)(void*, D3DPRESENT_PARAMETERS*);
    SetRTFn   g_origSetRT    = nullptr;
    SetDSFn   g_origSetDS    = nullptr;
    GxSetRTFn g_origGxSetRT  = nullptr;
    ResetFn   g_origReset    = nullptr;
    off::GxSetProjectionFn g_origGxSetProjection = nullptr;  // engine projection-upload trampoline (observer)
    off::GlueModelRenderFn g_origGlueModelRender = nullptr;  // CSimpleModelFFX::Render trampoline (glue boundary)
    void*     g_hookedDevice = nullptr;                // device whose vtable currently carries the render hooks

    // A depth-stencil that is also shader-readable, single-sample only.
    const D3DFORMAT    kFmtINTZ = static_cast<D3DFORMAT>(MAKEFOURCC('I', 'N', 'T', 'Z'));
    IDirect3DTexture9* g_ssaaColorTex  = nullptr;      // render-size world color TEXTURE (supersampling on)
    IDirect3DSurface9* g_ssaaColor     = nullptr;      // its level-0 surface, bound as the world render target
    IDirect3DTexture9* g_worldDepthTex = nullptr;      // render-size sampleable depth texture
    IDirect3DSurface9* g_worldDepth    = nullptr;      // its level-0 surface, bound as the world depth-stencil
    void*              g_targetDevice  = nullptr;      // device the offscreen surfaces belong to
    UINT g_nativeW = 0, g_nativeH = 0;                 // native backbuffer size
    UINT g_renderW = 0, g_renderH = 0;                 // offscreen render size (native * renderScale)
    bool g_hasColor = false;                           // a color render target exists (supersampling on)
    bool g_readableDepth = false;                      // the depth is the sampleable format vs a plain depth-stencil
    bool g_colorRedirect = false;                      // armed: redirect the color bind to g_ssaaColor
    bool g_depthRedirect = false;                      // armed: redirect the depth bind to g_worldDepth
    bool g_depthNeeded   = false;                      // a depth-using effect is enabled (set by the module)

    // Glue scene projection snapshot. The glue 3D camera is not in the world camera globals (those stay
    // identity on the glue screens), so the engine projection upload is observed during the glue model render
    // (hkGxSetProjection) and the perspective matrix captured here, then handed through OnWorldRenderEnd to a
    // depth-using effect (ambient occlusion). Row-major float[16], same layout as the world camera projection.
    float g_glueProj[16]  = {};
    bool  g_glueProjValid = false;

    /** @brief gx graphics-device object base, or null. */
    uint8_t* GxBase() { return reinterpret_cast<uint8_t*>(*reinterpret_cast<void**>(off::kGxDevicePtr)); }

    /** @brief True when a surface matches the native backbuffer size (so it should be redirected). */
    bool IsNativeSize(IDirect3DSurface9* s)
    {
        if (!s) return false;
        D3DSURFACE_DESC d{};
        return SUCCEEDED(s->GetDesc(&d)) && d.Width == g_nativeW && d.Height == g_nativeH;
    }

    /** @brief Redirects a native-size color bind to the render-size color target and scales the viewport. */
    long __stdcall hkSetRenderTarget(void* dev, unsigned long index, void* surface)
    {
        void* use = surface;
        if (g_colorRedirect && index == 0 && g_ssaaColor && IsNativeSize(static_cast<IDirect3DSurface9*>(surface)))
        {
            use = g_ssaaColor;
            // Bind the render-size depth BEFORE the render-size color: D3D9 validates a new render target against
            // the currently-bound depth-stencil, and a 2x color over a native (1x) depth makes it reject/unbind
            // the depth, so the world would render with no depth test (all geometry culled -> black).
            if (g_worldDepth) g_origSetDS(dev, g_worldDepth);
            if (uint8_t* base = GxBase())
            {
                *reinterpret_cast<float*>(base + off::kCurWindowWidth)  = (float)g_renderW;
                *reinterpret_cast<float*>(base + off::kCurWindowHeight) = (float)g_renderH;
                *reinterpret_cast<int*>(base + off::kViewportDirty)     = 1;
            }
        }
        return g_origSetRT(dev, index, use);
    }

    /** @brief Redirects a native-size depth bind to the render-size depth surface. */
    long __stdcall hkSetDepthStencil(void* dev, void* surface)
    {
        void* use = surface;
        if (g_depthRedirect && g_worldDepth && IsNativeSize(static_cast<IDirect3DSurface9*>(surface)))
            use = g_worldDepth;
        return g_origSetDS(dev, use);
    }

    /**
     * @brief Engine render-target bind, hooked as a pure observer. Supersampling is redirected ONLY through the
     *        D3D9 SetRenderTarget filter (the engine's own SetRenderTarget internally calls the hooked D3D9 bind,
     *        which hkSetRenderTarget redirects); an additional engine-level override here corrupted the engine's
     *        internal render-target state and broke the world render, so this bind is left untouched -- matching
     *        the known-good standalone build, whose engine-level hook is observer-only too. Kept hooked so the
     *        trampoline stays available if an effect ever needs to watch engine target switches.
     * @param self  graphics-device instance.
     * @param edx   unused register slot for the thiscall convention.
     * @param slot  render-target slot.
     * @param rt    engine render-target handle.
     * @param face  cube face.
     */
    void __fastcall hkGxSetRT(void* self, void* edx, int slot, int rt, int face)
    {
        g_origGxSetRT(self, edx, slot, rt, face);
        (void)slot; (void)rt; (void)face; (void)edx;
    }

    /**
     * @brief Engine projection-upload bind, hooked as a pure observer. The camera setup uploads its perspective
     *        projection through this device slot; the world render keeps reading the live world camera global,
     *        but the GLUE 3D camera never lands in that global (it stays identity on the glue screens), so the
     *        glue model's projection is snapshotted here while the glue render is in flight (g_inGlueRender),
     *        filtered to perspective uploads (proj[11] ~ 1; the restore-to-ortho upload at the model draw's end
     *        has proj[11] == 0 and is skipped). A depth-using effect (ambient occlusion) on the glue screens
     *        then gets the real matrix through OnWorldRenderEnd.
     * @param self    graphics-device instance.
     * @param edx     unused register slot for the thiscall convention.
     * @param proj16  the projection being uploaded: a row-major float[16].
     */
    void __fastcall hkGxSetProjection(void* self, void* edx, const void* proj16)
    {
        g_origGxSetProjection(self, edx, proj16);
        if (g_inGlueRender && proj16)
        {
            const float* p = static_cast<const float*>(proj16);
            if (p[11] > 0.5f)   // perspective (w=z term is 1.0); skip ortho / identity uploads
            {
                for (int i = 0; i < 16; ++i) g_glueProj[i] = p[i];
                g_glueProjValid = true;
            }
        }
        (void)edx;
    }

    /** @brief Releases the offscreen world surfaces. */
    void ReleaseTargets()
    {
        if (g_ssaaColor)     { g_ssaaColor->Release();     g_ssaaColor = nullptr; }
        if (g_ssaaColorTex)  { g_ssaaColorTex->Release();  g_ssaaColorTex = nullptr; }
        if (g_worldDepth)    { g_worldDepth->Release();    g_worldDepth = nullptr; }
        if (g_worldDepthTex) { g_worldDepthTex->Release(); g_worldDepthTex = nullptr; }
        g_nativeW = g_nativeH = g_renderW = g_renderH = 0;
        g_hasColor = g_readableDepth = false;
        g_targetDevice = nullptr;
    }

    /**
     * @brief Creates (or recreates on change) the world depth (INTZ + sampleable when readable, else a plain
     *        depth-stencil) plus the color render target when makeColor (supersampling). Render size =
     *        native * renderScale. Supersampling alone uses a plain depth; INTZ is created only when a depth
     *        effect needs to sample it.
     */
    bool EnsureTargets(IDirect3DDevice9* dev, UINT nativeW, UINT nativeH, float renderScale, bool makeColor, bool readable, D3DFORMAT colorFmt, D3DFORMAT depthFmt)
    {
        const UINT rw = (UINT)(nativeW * renderScale + 0.5f);
        const UINT rh = (UINT)(nativeH * renderScale + 0.5f);
        if (g_worldDepth && g_targetDevice == dev && g_nativeW == nativeW && g_nativeH == nativeH &&
            g_renderW == rw && g_renderH == rh && g_hasColor == makeColor && g_readableDepth == readable)
            return true;
        ReleaseTargets();
        if (readable)
        {
            // INTZ: a depth-stencil the world writes AND a shader-readable texture the AO samples.
            if (FAILED(dev->CreateTexture(rw, rh, 1, D3DUSAGE_DEPTHSTENCIL, kFmtINTZ, D3DPOOL_DEFAULT, &g_worldDepthTex, nullptr)) || !g_worldDepthTex ||
                FAILED(g_worldDepthTex->GetSurfaceLevel(0, &g_worldDepth)) || !g_worldDepth)
            {
                WLOG_WARN("ssaa: INTZ depth %ux%u failed", rw, rh);
                ReleaseTargets();
                return false;
            }
        }
        else if (FAILED(dev->CreateDepthStencilSurface(rw, rh, depthFmt, D3DMULTISAMPLE_NONE, 0, FALSE, &g_worldDepth, nullptr)) || !g_worldDepth)
        {
            WLOG_WARN("ssaa: CreateDepthStencilSurface %ux%u fmt=%d failed", rw, rh, (int)depthFmt);
            ReleaseTargets();
            return false;
        }
        // Color as a plain render-target surface (CreateRenderTarget), matching the known-good standalone
        // implementation: the world renders into it AND the post-process samples it through On12's unwrap. The
        // earlier D3DUSAGE_RENDERTARGET *texture* made On12 hand the module an empty (black) resource; a plain
        // render-target surface unwraps to the resource the world actually drew into and samples correctly.
        if (makeColor &&
            (FAILED(dev->CreateRenderTarget(rw, rh, colorFmt, D3DMULTISAMPLE_NONE, 0, FALSE, &g_ssaaColor, nullptr)) || !g_ssaaColor))
        {
            WLOG_WARN("ssaa: color render target %ux%u fmt=%d failed", rw, rh, (int)colorFmt);
            ReleaseTargets();
            return false;
        }
        g_targetDevice = dev;
        g_nativeW = nativeW; g_nativeH = nativeH;
        g_renderW = rw; g_renderH = rh;
        g_hasColor = makeColor;
        g_readableDepth = readable;
        WLOG_INFO("ssaa: targets %ux%u (native %ux%u) color=%d depth=%s", rw, rh, nativeW, nativeH, makeColor ? (int)colorFmt : -1, readable ? "INTZ" : "plain");
        return true;
    }

    /** @brief Installs (or re-installs after a device recreate) all render vtable hooks on the live device. */
    void EnsureDeviceHooks(IDirect3DDevice9* dev);   // defined after SwapVtbl

    /**
     * @brief Per-frame setup, called at present for the coming frame: ensures the offscreen surfaces exist and
     *        arms the redirect flags. Supersampling redirects the color into a render-size surface and uses a
     *        render-size depth; a depth-using effect alone keeps the native color and uses a native-size
     *        sampleable depth. The depth is the sampleable format when a depth-using effect is enabled.
     */
    void SsaaArmFrame()
    {
        const float factor  = WxlGetSsaaFactor();
        const bool  ssaaOn  = factor > 1.01f;
        // The SSAA/depth redirect is in-world ONLY: it relies on the world pass clearing + rendering into the
        // offscreen and on the world hook resolving it back. The glue screens (login / character select) have no
        // such world pass, so redirecting there would send the glue 3D into the offscreen with nothing to resolve
        // it (black screen). g_worldActive (set by the world hook this frame) gates it. Post-process AA still runs
        // on glue via the general UI-quad boundary, sampling the backbuffer directly.
        const bool  active  = (ssaaOn || g_depthNeeded) && g_worldActive;
        if (!active) { g_colorRedirect = g_depthRedirect = false; return; }

        IDirect3DDevice9* dev = static_cast<IDirect3DDevice9*>(gx::RawDevice());
        uint8_t* base = GxBase();
        if (!dev || !base) { g_colorRedirect = g_depthRedirect = false; return; }
        EnsureDeviceHooks(dev);

        IDirect3DSurface9* bb = nullptr;
        if (FAILED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) || !bb) { g_colorRedirect = g_depthRedirect = false; return; }
        D3DSURFACE_DESC cd{};
        bb->GetDesc(&cd);
        bb->Release();

        // Engine MSAA: the redirect is incompatible -- a single-sample offscreen color (supersampling) or a
        // single-sample readable depth (ambient occlusion) cannot pair with a multisampled world target, and the
        // post-process passes multisampled frames through untouched. Leave the native targets in place so the
        // engine's own multisampling is the anti-aliasing.
        if (cd.MultiSampleType != D3DMULTISAMPLE_NONE)
        {
            g_colorRedirect = g_depthRedirect = false;
            return;
        }

        // Plain depth format taken from the native depth surface (ignored when the sampleable format is used).
        D3DFORMAT depthFmt = D3DFMT_D24X8;
        if (IDirect3DSurface9* nd = *reinterpret_cast<IDirect3DSurface9**>(base + off::kDepthSurfaceField))
        {
            D3DSURFACE_DESC dd{};
            if (SUCCEEDED(nd->GetDesc(&dd))) depthFmt = dd.Format;
        }

        // Color is redirected only for supersampling; a depth-only frame keeps the native color and redirects
        // the depth alone. The depth uses the sampleable format when a depth-using effect is enabled.
        const bool  makeColor   = ssaaOn;
        const bool  readable    = g_depthNeeded;
        const float renderScale = ssaaOn ? factor : 1.0f;
        if (!EnsureTargets(dev, cd.Width, cd.Height, renderScale, makeColor, readable, cd.Format, depthFmt))
        {
            g_colorRedirect = g_depthRedirect = false;
            return;
        }
        g_colorRedirect = makeColor;
        g_depthRedirect = true;

        // Actively bind the offscreen targets NOW for the coming frame -- this is the piece the known-good
        // standalone build had (Ssaa::Arm) that a catch-the-bind redirect alone lacks. The engine does not
        // reliably re-issue a native-size backbuffer SetRenderTarget at the world-pass start, so without an
        // active bind the world keeps rendering into the real backbuffer and g_ssaaColor stays empty (and the
        // resolve then clobbers the good backbuffer with that empty surface -> black both ways). Binding here
        // makes the offscreen the current target before the next frame's clear and world draw; the engine's own
        // frame clear then lands on the offscreen (so there is no stale-depth cull and no separate clear is
        // needed). The D3D9 render-target / depth filters still catch any engine re-bind mid-pass and keep it
        // redirected to the offscreen.
        if (g_origSetRT && makeColor && g_ssaaColor)
        {
            g_origSetRT(dev, 0, g_ssaaColor);
            if (g_origSetDS && g_worldDepth) g_origSetDS(dev, g_worldDepth);
            // Clear the offscreen color + depth for the coming frame (defensive: the engine also clears it, but
            // this guarantees a clean start independent of engine state).
            dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, 1.0f, 0);
            *reinterpret_cast<float*>(base + off::kCurWindowWidth)  = (float)g_renderW;
            *reinterpret_cast<float*>(base + off::kCurWindowHeight) = (float)g_renderH;
            *reinterpret_cast<int*>(base + off::kViewportDirty)     = 1;
        }
        else if (g_origSetDS && g_worldDepth)
        {
            // Depth-only (a depth effect without supersampling): keep the native color, bind the readable depth.
            g_origSetDS(dev, g_worldDepth);
        }
    }

    /**
     * @brief Detours world-frame finalize: the world -> UI boundary. When the redirect was armed this frame
     *        the world has rendered into the offscreen surfaces; here the render-size world color is resolved
     *        (downsampled) onto the native backbuffer, the redirect is disarmed, the native render target /
     *        depth / curWindow are restored for the UI, then OnWorldRenderEnd is fired. A depth-using effect
     *        (ambient occlusion) gets the sampleable world depth through the event for a module-side D3D12 pass.
     * @param worldFrame  world frame being finalized.
     */
    void __cdecl hkWorldFinalize(void* worldFrame)
    {
        // Re-install the render vtable hooks on the live device (survives a device recreate).
        if (IDirect3DDevice9* d = static_cast<IDirect3DDevice9*>(gx::RawDevice()))
            EnsureDeviceHooks(d);

        g_origWorldFinalize(worldFrame);

        // In-world: claim the once-per-frame boundary so the general UI-quad hook (hkGxDeviceDraw) no-ops this
        // frame, and mark that we are in-world so SsaaArmFrame may arm the redirect for the next frame.
        g_boundaryFired = true;
        g_worldActive   = true;

        // World to UI boundary. Resolve the render-size world color onto the native backbuffer, then disarm the
        // redirect and restore the native render target, depth, and viewport so the UI draws at native resolution.
        const bool colorR = g_colorRedirect;
        const bool depthR = g_depthRedirect;
        if (colorR || depthR)
        {
            g_colorRedirect = g_depthRedirect = false;
            IDirect3DDevice9* dev  = static_cast<IDirect3DDevice9*>(gx::RawDevice());
            uint8_t*          base = GxBase();
            if (dev && colorR)
            {
                IDirect3DSurface9* bb = nullptr;
                if (SUCCEEDED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb)
                {
                    // Resolve the render-size world color down onto the native backbuffer. A linear-filtered
                    // StretchRect from the 2x surface is the supersampling box-downsample (a 2x2 average for an
                    // integer 2x factor); the On12 proxy handles this surface->backbuffer blit reliably. The UI
                    // then draws at native resolution over the resolved frame.
                    dev->StretchRect(g_ssaaColor, nullptr, bb, nullptr, D3DTEXF_LINEAR);
                    g_origSetRT(dev, 0, bb);
                    bb->Release();
                }
            }
            if (dev && depthR && base)
                if (IDirect3DSurface9* nd = *reinterpret_cast<IDirect3DSurface9**>(base + off::kDepthSurfaceField))
                    g_origSetDS(dev, nd);
            if (colorR && base)
            {
                *reinterpret_cast<float*>(base + off::kCurWindowWidth)  = (float)g_nativeW;
                *reinterpret_cast<float*>(base + off::kCurWindowHeight) = (float)g_nativeH;
                *reinterpret_cast<int*>(base + off::kViewportDirty)     = 1;
            }
        }

        // The core already resolved supersampling onto the backbuffer (the StretchRect above), so the module is
        // handed a null supersample source for plain SSAA -- it has nothing to do. depthSource is the sampleable
        // world depth, passed when a depth-using effect (ambient occlusion) asked for it, so the module can run
        // its D3D12 pass. (A post-process AA effect -- FXAA / CMAA / SMAA -- will instead take over the resolve:
        // the core hands it g_ssaaColor and skips the StretchRect, so the shader does the downsample + AA.)
        ev::WorldRenderEndArgs a{ gx::RawDevice(),
                                  nullptr,
                                  colorR ? WxlGetSsaaFactor() : 1.0f,
                                  (depthR && g_readableDepth) ? static_cast<void*>(g_worldDepth) : nullptr,
                                  nullptr };   // in-world the subscriber reads the live world camera projection
        ev::Emit(ev::Event::OnWorldRenderEnd, &a);
    }

    /**
     * @brief Arms the redirect for the glue 3D scene (login / character-select model preview), the glue-side
     *        analogue of SsaaArmFrame + the world arm. CSimpleModelFFX::Render binds no render target of its own
     *        and drives its viewport through the engine curWindow rect (the same path the world uses), so binding
     *        the offscreen here and scaling curWindow makes the glue scene rasterize at render size; GlueResolve
     *        resolves it back at the boundary. Two independent arms, like the world:
     *          - supersampling (factor > 1): redirect the color into a render-size offscreen + scale curWindow.
     *            Unlike the world (which clears the whole frame into the offscreen), the glue pass renders the
     *            model into a sub-rect with no full-frame clear, so the current backbuffer is copied (upscaled)
     *            into the offscreen first, and the full-frame resolve preserves the 2D backdrop.
     *          - depth-using effect (ambient occlusion): bind a sampleable INTZ depth (render-size with
     *            supersampling, else native) so the glue model writes a depth the module can sample.
     *        Sets g_colorRedirect / g_depthRedirect. Returns true when anything was armed (no engine MSAA).
     */
    bool GlueArm()
    {
        const float factor = WxlGetSsaaFactor();
        const bool  ssaaOn = factor > 1.01f;
        if (!ssaaOn && !g_depthNeeded) return false;   // nothing to do: plain post-process AA samples the bb

        IDirect3DDevice9* dev  = static_cast<IDirect3DDevice9*>(gx::RawDevice());
        uint8_t*          base = GxBase();
        if (!dev || !base) return false;
        EnsureDeviceHooks(dev);

        IDirect3DSurface9* bb = nullptr;
        if (FAILED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) || !bb) return false;
        D3DSURFACE_DESC cd{};
        bb->GetDesc(&cd);
        // Engine MSAA: a single-sample offscreen / readable depth cannot pair with a multisampled target.
        if (cd.MultiSampleType != D3DMULTISAMPLE_NONE) { bb->Release(); return false; }

        D3DFORMAT depthFmt = D3DFMT_D24X8;
        if (IDirect3DSurface9* nd = *reinterpret_cast<IDirect3DSurface9**>(base + off::kDepthSurfaceField))
        {
            D3DSURFACE_DESC dd{};
            if (SUCCEEDED(nd->GetDesc(&dd))) depthFmt = dd.Format;
        }

        const bool  makeColor   = ssaaOn;        // color offscreen only for supersampling
        const bool  readable    = g_depthNeeded; // sampleable INTZ depth only for a depth-using effect
        const float renderScale = ssaaOn ? factor : 1.0f;
        if (!EnsureTargets(dev, cd.Width, cd.Height, renderScale, makeColor, readable, cd.Format, depthFmt))
        {
            bb->Release();
            return false;
        }

        g_colorRedirect = makeColor;
        g_depthRedirect = true;
        if (makeColor && g_ssaaColor)
        {
            // Supersampling: seed the offscreen with the current backbuffer (upscaled) so the 2D backdrop drawn
            // before the model survives the full-frame resolve, then bind the offscreen + render-size depth and
            // scale curWindow so the glue scene rasterizes at render size.
            dev->StretchRect(bb, nullptr, g_ssaaColor, nullptr, D3DTEXF_LINEAR);
            g_origSetRT(dev, 0, g_ssaaColor);
            if (g_worldDepth) g_origSetDS(dev, g_worldDepth);
            *reinterpret_cast<float*>(base + off::kCurWindowWidth)  = (float)g_renderW;
            *reinterpret_cast<float*>(base + off::kCurWindowHeight) = (float)g_renderH;
            *reinterpret_cast<int*>(base + off::kViewportDirty)     = 1;
        }
        else if (g_worldDepth)
        {
            // Depth-only (ambient occlusion without supersampling): keep the native color, bind the readable
            // depth so the glue model writes a sampleable depth; the color is untouched.
            g_origSetDS(dev, g_worldDepth);
        }
        bb->Release();
        return true;
    }

    /**
     * @brief Resolves the glue redirect and restores the native render target / depth / viewport -- the
     *        glue-side analogue of the resolve block in hkWorldFinalize. Called only when GlueArm armed.
     * @param color  true when the color was redirected into the offscreen (supersampling): downsample it back.
     */
    void GlueResolve(bool color)
    {
        IDirect3DDevice9* dev  = static_cast<IDirect3DDevice9*>(gx::RawDevice());
        uint8_t*          base = GxBase();
        g_colorRedirect = g_depthRedirect = false;
        if (!dev) return;

        if (color)
        {
            IDirect3DSurface9* bb = nullptr;
            if (SUCCEEDED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb)
            {
                dev->StretchRect(g_ssaaColor, nullptr, bb, nullptr, D3DTEXF_LINEAR);
                g_origSetRT(dev, 0, bb);
                bb->Release();
            }
        }
        // Restore the native depth (bound for both the supersampling and depth-only arms). The module still
        // unwraps the INTZ surface directly, so it does not need it bound as the D3D9 depth-stencil afterward.
        if (base)
        {
            if (IDirect3DSurface9* nd = *reinterpret_cast<IDirect3DSurface9**>(base + off::kDepthSurfaceField))
                g_origSetDS(dev, nd);
            if (color)
            {
                *reinterpret_cast<float*>(base + off::kCurWindowWidth)  = (float)g_nativeW;
                *reinterpret_cast<float*>(base + off::kCurWindowHeight) = (float)g_nativeH;
                *reinterpret_cast<int*>(base + off::kViewportDirty)     = 1;
            }
        }
    }

    /**
     * @brief CSimpleModelFFX::Render inline hook: the GLUE world -> UI boundary, the glue-side analogue of the
     *        world-only hkWorldFinalize. The engine defers every 3D render into a per-frame-object callback, so
     *        there is no single global "3D done" point: the world hook covers the world, this covers the glue
     *        model (login / character-select preview). The boundary fires AFTER the model renders (its 3D is on
     *        the backbuffer, the glue UI not yet drawn), once per frame via the shared latch. In-world this also
     *        runs for 3D UI portraits, but the world hook already claimed the boundary, so it is a no-op there.
     *        GlueArm/GlueResolve bracket the model render: supersampling rasterizes the glue 3D into the
     *        render-size offscreen and downsamples it back, and a depth-using effect gets a sampleable glue
     *        depth. The glue camera's projection is snapshotted during the render (hkGxSetProjection, gated by
     *        g_inGlueRender) and handed to the effect through the event, since the glue camera is not in the
     *        world camera globals. The post-process then runs on the resolved native backbuffer.
     * @param frame  the CSimpleModelFFX frame being rendered (passed through; the redirect is bound around it).
     */
    void __cdecl hkGlueModelRender(void* frame)
    {
        // Claim the once-per-frame boundary up front so the arm/resolve pair brackets exactly the first glue
        // 3D callback of the frame (a glue frame can run several; only the first is the world->UI boundary).
        const bool claim = !g_boundaryFired && !g_inBoundary;
        bool armedColor = false, armedDepth = false;
        if (claim && GlueArm()) { armedColor = g_colorRedirect; armedDepth = g_depthRedirect; }

        // Observe the glue projection upload only for this claiming render, captured fresh each frame.
        if (claim) { g_glueProjValid = false; g_inGlueRender = true; }
        g_origGlueModelRender(frame);   // render the glue 3D model (into the offscreen / readable depth when armed)
        if (claim) g_inGlueRender = false;

        if (claim)
        {
            g_boundaryFired = true;
            g_inBoundary    = true;
            if (armedColor || armedDepth) GlueResolve(armedColor);
            // Supersampling is resolved here (StretchRect), so superSampleSource is null. depthSource is the
            // sampleable glue depth when a depth-using effect armed it; proj is the snapshotted glue projection
            // (the world camera globals are identity on glue), used by the module in place of the live camera.
            void*        depth = (armedDepth && g_readableDepth) ? static_cast<void*>(g_worldDepth) : nullptr;
            const float* proj  = (armedDepth && g_glueProjValid) ? g_glueProj : nullptr;
            ev::WorldRenderEndArgs a{ gx::RawDevice(), nullptr,
                                      armedColor ? WxlGetSsaaFactor() : 1.0f, depth, proj };
            ev::Emit(ev::Event::OnWorldRenderEnd, &a);
            g_inBoundary = false;
        }
    }

    /**
     * @brief Detours the per-frame liquid render pass, emitting OnLiquidRender before the native draw.
     *
     * Fires once per pass (passType 0 main, 1 secondary). The liquid textures are already bound and the
     * wave animation already applied at this point. Logs the first fire of each pass, then stays quiet.
     * @param bank       liquid material-settings bank (this-in-ECX), indexed by passType.
     * @param edx        unused register slot for the thiscall convention.
     * @param transform  shared liquid transform forwarded to every instance draw.
     * @param passType   liquid pass index (0 main, 1 secondary).
     */
    void __fastcall hkLiquidRender(void* bank, void* edx, void* transform, int passType)
    {
        const off::LiquidPassEntry& entry = static_cast<off::LiquidPassEntry*>(bank)[passType];

        ev::LiquidRenderArgs a{ bank, transform, passType, entry.count };
        ev::Emit(ev::Event::OnLiquidRender, &a);

        g_origLiquidRender(bank, edx, transform, passType);
    }

    /** @brief Returns the graphics-device object carrying the engine sampler-bind path (distinct from the D3D9 device). */
    void* GxDeviceObject() { return *reinterpret_cast<void**>(off::kGxDevicePtr); }

    /**
     * @brief Binds ribbon layers 1 and 2 to samplers s1/s2 through the engine for the single pass.
     *
     * Called only with layerCount >= 3, so layers [1] and [2] are in range.
     * @param gxDev    graphics-device object.
     * @param emitter  ribbon emitter holding the texture handle array.
     * @return true when both layers resolved and bound.
     */
    bool BindRibbonExtraSamplers(void* gxDev, const uint8_t* emitter)
    {
        const void* const* arr = reinterpret_cast<const m2off::RibbonEmitter*>(emitter)->texHandles;
        if (!arr) return false;
        void* h1 = const_cast<void*>(arr[1]);
        void* h2 = const_cast<void*>(arr[2]);
        if (!h1 || !h2) return false;

        auto resolve = reinterpret_cast<m2off::M2_TexResolveFn>(m2off::kTexResolve);
        auto bind    = reinterpret_cast<m2off::M2_SamplerBindFn>(m2off::kSamplerBind);
        void* t1 = resolve(h1, 0, 0);
        void* t2 = resolve(h2, 0, 0);
        if (!t1 || !t2) return false;
        bind(gxDev, nullptr, m2off::kSamplerSelS1, t1);
        bind(gxDev, nullptr, m2off::kSamplerSelS2, t2);
        return true;
    }

    /**
     * @brief Detours the ribbon emitter draw, emitting OnRibbonDraw and optionally folding layers.
     *
     * When a subscriber opts a three-or-more-layer ribbon into the multi-texture combine, pre-binds
     * s1/s2, flags the draw so the DIP override folds the layers, and clamps the layer count to 1 so
     * the native draw runs exactly one pass. Otherwise the draw runs untouched.
     * @param self        ribbon emitter.
     * @param edx         unused register slot for the thiscall convention.
     * @param stateBlock  native render state block.
     * @return the native ribbon-draw result.
     */
    int __fastcall hkRibbonDraw(void* self, void* edx, void* stateBlock)
    {
        g_ribbonModern = false;

        uint8_t*  emitter       = static_cast<uint8_t*>(self);
        bool      bridged       = false;
        uint32_t  savedLayers   = 0;
        uint32_t* layerCountPtr = nullptr;

        if (emitter)
        {
            __try
            {
                layerCountPtr = &reinterpret_cast<m2off::RibbonEmitter*>(emitter)->layerCount;
                uint32_t layers = *layerCountPtr;

                bool useMulti = false;
                ev::RibbonDrawArgs a{ emitter, layers, &useMulti };
                ev::Emit(ev::Event::OnRibbonDraw, &a);

                if (useMulti && layers >= 3)
                {
                    void* gxDev = GxDeviceObject();
                    if (gxDev && BindRibbonExtraSamplers(gxDev, emitter))
                    {
                        g_ribbonModern = true;
                        savedLayers    = layers;
                        *layerCountPtr = 1;
                        bridged        = true;
                    }
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { bridged = false; g_ribbonModern = false; }
        }

        int r = g_origRibbonDraw(self, edx, stateBlock);

        if (bridged && layerCountPtr)
            __try { *layerCountPtr = savedLayers; } __except (EXCEPTION_EXECUTE_HANDLER) {}
        g_ribbonModern = false;
        return r;
    }

    /**
     * @brief Replaces one vtable entry with a hook, returning the original through origOut.
     * @param vtbl     vtable base.
     * @param idx      entry index to swap.
     * @param hook     replacement function pointer.
     * @param origOut  receives the original entry.
     */
    void SwapVtbl(void** vtbl, unsigned idx, void* hook, void** origOut)
    {
        DWORD old;
        VirtualProtect(&vtbl[idx], sizeof(void*), PAGE_EXECUTE_READWRITE, &old);
        *origOut = vtbl[idx];
        vtbl[idx] = hook;
        VirtualProtect(&vtbl[idx], sizeof(void*), old, &old);
    }

    /**
     * @brief Detours IDirect3DDevice9::Reset (a resolution / window resize). D3D9 requires every
     *        D3DPOOL_DEFAULT resource released before a Reset or it fails (D3DERR_INVALIDCALL) and the device is
     *        lost -- the resize "crash". Disarm the supersampling redirect and free the offscreen world surfaces
     *        (g_ssaaColor / g_worldDepth, both DEFAULT pool) plus any tracked engine render targets, fire
     *        OnDeviceLost, run the native Reset, then fire OnDeviceReset on success so subscribers recreate. The
     *        SSAA surfaces themselves recreate lazily on the next SsaaArmFrame, at the new backbuffer size.
     * @param dev     the D3D9 device being reset.
     * @param params  the present parameters the device resets with (new size / window mode).
     * @return the native Reset result.
     */
    long __stdcall hkReset(void* dev, D3DPRESENT_PARAMETERS* params)
    {
        static unsigned resetLog = 0;
        const bool logThis = resetLog < 16;
        if (logThis)
        {
            ++resetLog;
            WLOG_INFO("render: Reset begin %ux%u windowed=%u",
                      params ? params->BackBufferWidth : 0,
                      params ? params->BackBufferHeight : 0,
                      params ? static_cast<unsigned>(params->Windowed) : 0);
        }

        ev::DeviceResetArgs a{ dev, params };
        g_colorRedirect = g_depthRedirect = false;
        // Subscribers using the engine-owned color/depth surfaces must retire their GPU work before those
        // resources are released. In particular, wxl-modern-render returns On12 borrows with a fence from its
        // own queue; releasing first races D3D9On12's native Reset and has crashed inside the NVIDIA driver.
        ev::Emit(ev::Event::OnDeviceLost, &a);
        if (logThis) WLOG_INFO("render: Reset drained subscribers");
        ReleaseTargets();             // free the offscreen SSAA color + world depth (DEFAULT pool)
        if (logThis) WLOG_INFO("render: Reset released world targets");
        gx::ReleaseResetResources();  // free any tracked engine render targets (DEFAULT pool)
        if (logThis) WLOG_INFO("render: Reset released tracked targets");
        if (logThis) WLOG_INFO("render: Reset entering native device");

        const long r = g_origReset(dev, params);
        if (SUCCEEDED(r))
        {
            ev::Emit(ev::Event::OnDeviceReset, &a);
            if (logThis) WLOG_INFO("render: Reset ok");
        }
        else
        {
            static unsigned logged = 0;
            if (logged < 8)
            {
                ++logged;
                WLOG_WARN("render: Reset failed hr=0x%08lX", static_cast<unsigned long>(r));
            }
        }
        return r;
    }

    // Installs all render vtable hooks on the live device. Each device instance may have its own vtable, so on
    // a device recreate the swaps are gone; this re-applies them on the current device. The function-entry
    // render hook survives the recreate and calls this each frame, so OnEndScene/OnFrame and the supersampling
    // filters keep working after a restart. Guarded against the case where On12 hands several device instances
    // one shared vtable: if it already carries our hooks, re-swapping would capture our own hook as the
    // "original" and recurse, so only the device pointer is updated.
    void EnsureDeviceHooks(IDirect3DDevice9* dev)
    {
        if (!dev || g_hookedDevice == dev) return;
        void** vtbl = *reinterpret_cast<void***>(dev);
        if (vtbl[off::vt::kPresent] != reinterpret_cast<void*>(&hkPresent))
        {
            SwapVtbl(vtbl, off::vt::kDrawIndexedPrimitive, reinterpret_cast<void*>(&hkDIP),             reinterpret_cast<void**>(&g_origDIP));
            SwapVtbl(vtbl, off::vt::kEndScene,             reinterpret_cast<void*>(&hkEndScene),        reinterpret_cast<void**>(&g_origEndScene));
            SwapVtbl(vtbl, off::vt::kPresent,              reinterpret_cast<void*>(&hkPresent),         reinterpret_cast<void**>(&g_origPresent));
            SwapVtbl(vtbl, off::vt::kSetRenderTarget,      reinterpret_cast<void*>(&hkSetRenderTarget), reinterpret_cast<void**>(&g_origSetRT));
            SwapVtbl(vtbl, off::vt::kSetDepthStencil,      reinterpret_cast<void*>(&hkSetDepthStencil), reinterpret_cast<void**>(&g_origSetDS));
            SwapVtbl(vtbl, off::vt::kReset,                reinterpret_cast<void*>(&hkReset),           reinterpret_cast<void**>(&g_origReset));
            WLOG_INFO("render: device hooks installed (dev=%p)", (void*)dev);
        }
        g_hookedDevice = dev;
    }
}

namespace wxl::runtime::render
{
    /**
     * @brief Installs the render detours via vtable swaps and function-entry hooks.
     */
    void Install()
    {
        void* dev = gx::RawDevice();
        if (!dev) { WLOG_WARN("render: device not up, hooks skipped"); return; }

        // Vtable hooks (DIP, EndScene, Present, SetRenderTarget, SetDepthStencil); re-applied on a device
        // recreate from the surviving world-render function-entry hook (see EnsureDeviceHooks / hkWorldFinalize).
        EnsureDeviceHooks(static_cast<IDirect3DDevice9*>(dev));

        // Engine render-target bind + projection upload, a static vtable shared across device recreates,
        // swapped once. The projection slot is observer-only: it snapshots the glue scene's projection for a
        // depth-using effect (the glue camera is not in the world camera globals).
        if (!g_origGxSetRT)
        {
            void** gxVtbl = reinterpret_cast<void**>(off::kGxDeviceVTable);
            SwapVtbl(gxVtbl, off::kGxSetRenderTargetSlot, reinterpret_cast<void*>(&hkGxSetRT),         reinterpret_cast<void**>(&g_origGxSetRT));
            SwapVtbl(gxVtbl, off::kGxSetProjectionSlot,   reinterpret_cast<void*>(&hkGxSetProjection), reinterpret_cast<void**>(&g_origGxSetProjection));
        }

        // Function-entry detours; enabled by the batch EnableAll() the caller runs after all installers.
        wxl::core::hook::Install("M2DrawBatch", off::kDrawTriangleBatch,
                                 reinterpret_cast<void*>(&hkDrawBatch),
                                 reinterpret_cast<void**>(&g_origDrawBatch));
        wxl::core::hook::Install("WorldRenderFinalize", off::kWorldRenderFinalize,
                                 reinterpret_cast<void*>(&hkWorldFinalize),
                                 reinterpret_cast<void**>(&g_origWorldFinalize));
        // Glue 3D-scene boundary (login / character-select model preview): the glue-side analogue of the world
        // finalize hook. Fires OnWorldRenderEnd after the glue model renders, so the post-process AA reaches the
        // glue screens without hooking the hot per-primitive CGxDevice::Draw path.
        wxl::core::hook::Install("GlueModelRender", off::kSimpleModelFFXRender,
                                 reinterpret_cast<void*>(&hkGlueModelRender),
                                 reinterpret_cast<void**>(&g_origGlueModelRender));
        wxl::core::hook::Install("M2RibbonDraw", m2off::kRibbonDraw,
                                 reinterpret_cast<void*>(&hkRibbonDraw),
                                 reinterpret_cast<void**>(&g_origRibbonDraw));
        wxl::core::hook::Install("LiquidRenderPass", off::kLiquidRenderPass,
                                 reinterpret_cast<void*>(&hkLiquidRender),
                                 reinterpret_cast<void**>(&g_origLiquidRender));

        WLOG_INFO("render: hooks installed (DIP, EndScene, Present, DrawBatch, WorldFinalize, GlueModelRender, RibbonDraw, LiquidRenderPass)");
    }

    void SetReadableDepthNeeded(bool needed)
    {
        g_depthNeeded = needed;
    }
}
