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

#include <windows.h>
#include <d3d9.h>

// Uses two device-vtable pointer swaps (DrawIndexedPrimitive, EndScene) and one function-entry hook
// (the M2 batch draw); no mid-function inline patch, so the world render pass stays intact. The
// world->UI post-fx slot is served from the EndScene hook instead.
namespace
{
    namespace off   = wxl::offsets::engine::gx;
    namespace ev    = wxl::events;
    namespace gx    = wxl::game::gx;
    namespace m2off = wxl::offsets::game::m2;

    // The model currently drawing, captured between a batch-draw enter and its DrawIndexedPrimitive.
    void* g_curModel = nullptr;
    // Re-entrancy guard: a subscriber re-issues the draw through the hooked vtable, so do not re-emit.
    bool  g_inM2Emit = false;

    using DrawBatchFn = void (__fastcall*)(void* ctx, void* edx);
    using DIPFn       = long (__stdcall*)(void*, int, int, unsigned, unsigned, unsigned, unsigned);
    using EndSceneFn  = long (__stdcall*)(void*);
    using PresentFn   = long (__stdcall*)(void*, const void*, const void*, void*, const void*);

    DrawBatchFn g_origDrawBatch = nullptr;
    DIPFn       g_origDIP       = nullptr;
    EndSceneFn  g_origEndScene  = nullptr;
    PresentFn   g_origPresent   = nullptr;
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
        g_curModel = static_cast<off::DrawBatchContext*>(ctx)->model;
        g_origDrawBatch(ctx, edx);
        g_curModel = nullptr;
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

        long r = g_origDIP(dev, pt, bv, mi, nv, si, pc);
        if (g_curModel && !g_inM2Emit)
        {
            g_inM2Emit = true;
            ev::M2BatchDrawArgs a{ dev, g_curModel, pt, bv, mi, nv, si, pc };
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
        // Arm the supersampling redirect for the next frame: the world renders before the world-finalize
        // boundary, so the SetRenderTarget filter must already be armed when the next frame binds its backbuffer.
        SsaaArmFrame();
        return g_origPresent(dev, src, dst, wnd, dirty);
    }

    // --- World-only supersampling via render-target redirect ------------------------------------------
    // The windowed On12 backbuffer is pinned to the window client size, so it cannot be enlarged. Instead the
    // world is redirected into a factor-sized offscreen color+depth surface, then downsampled into the native
    // backbuffer at the world -> UI boundary; the UI then draws crisp at native resolution. The world renders
    // EARLY in the frame (before the world-finalize callback) and binds the native backbuffer via
    // SetRenderTarget, so the redirect is a FILTER on SetRenderTarget(0)/SetDepthStencilSurface, armed from
    // the previous frame's Present through the world->UI boundary: every native-size backbuffer bind in that
    // window is swapped to the factor-sized surfaces, and curWindow is scaled so the engine's viewport
    // follows. At the boundary (world-finalize) the native targets/curWindow are restored for the UI and the
    // offscreen world is resolved into the native backbuffer in D3D12 by the OnWorldRenderEnd subscriber
    // (D3D9 StretchRect to the On12 backbuffer is rejected).
    // (RE: _docs/re_comprehension/335/ssaa_world_render_target.md; approach mirrors the archived
    // WotLK-Extensions Ssaa SetRenderTarget filter.)
    using SetRTFn = long (__stdcall*)(void*, unsigned long, void*);
    using SetDSFn = long (__stdcall*)(void*, void*);
    SetRTFn g_origSetRT = nullptr;
    SetDSFn g_origSetDS = nullptr;
    void*   g_filterDevice = nullptr;                  // device whose vtable currently carries the filters

    IDirect3DSurface9* g_ssaaColor = nullptr;          // factor-sized world color render target
    IDirect3DSurface9* g_ssaaDepth = nullptr;          // factor-sized world depth-stencil
    void*              g_ssaaDevice = nullptr;         // device the offscreen surfaces belong to
    UINT g_ssaaNativeW = 0, g_ssaaNativeH = 0;         // native backbuffer size the redirect matches
    UINT g_ssaa2xW = 0, g_ssaa2xH = 0;                 // offscreen (supersampled) size
    bool g_ssaaRedirect = false;                       // armed: native-size bb binds redirect to the 2x color

    /** @brief gx graphics-device object base, or null. */
    uint8_t* GxBase() { return reinterpret_cast<uint8_t*>(*reinterpret_cast<void**>(off::kGxDevicePtr)); }

    /** @brief True when a surface matches the native backbuffer size (so it should be redirected). */
    bool SsaaIsNativeSize(IDirect3DSurface9* s)
    {
        if (!s) return false;
        D3DSURFACE_DESC d{};
        return SUCCEEDED(s->GetDesc(&d)) && d.Width == g_ssaaNativeW && d.Height == g_ssaaNativeH;
    }

    /** @brief SetRenderTarget filter: while armed, swaps a native-size backbuffer bind for the 2x color. */
    long __stdcall hkSetRenderTarget(void* dev, unsigned long index, void* surface)
    {
        void* use = surface;
        if (g_ssaaRedirect && index == 0 && g_ssaaColor && SsaaIsNativeSize(static_cast<IDirect3DSurface9*>(surface)))
        {
            use = g_ssaaColor;
            if (uint8_t* base = GxBase())
            {
                *reinterpret_cast<float*>(base + off::kCurWindowWidth)  = (float)g_ssaa2xW;
                *reinterpret_cast<float*>(base + off::kCurWindowHeight) = (float)g_ssaa2xH;
                *reinterpret_cast<int*>(base + off::kViewportDirty)     = 1;
            }
        }
        return g_origSetRT(dev, index, use);
    }

    /** @brief SetDepthStencilSurface filter: while armed, swaps a native-size depth for the 2x depth. */
    long __stdcall hkSetDepthStencil(void* dev, void* surface)
    {
        void* use = surface;
        if (g_ssaaRedirect && g_ssaaDepth && SsaaIsNativeSize(static_cast<IDirect3DSurface9*>(surface)))
            use = g_ssaaDepth;
        return g_origSetDS(dev, use);
    }

    /** @brief Releases the offscreen world surfaces. */
    void SsaaReleaseTargets()
    {
        if (g_ssaaColor) { g_ssaaColor->Release(); g_ssaaColor = nullptr; }
        if (g_ssaaDepth) { g_ssaaDepth->Release(); g_ssaaDepth = nullptr; }
        g_ssaaNativeW = g_ssaaNativeH = g_ssaa2xW = g_ssaa2xH = 0;
        g_ssaaDevice = nullptr;
    }

    /** @brief Creates (or recreates on device/size change) the offscreen world surfaces. */
    bool SsaaEnsureTargets(IDirect3DDevice9* dev, UINT nativeW, UINT nativeH, float factor, D3DFORMAT colorFmt, D3DFORMAT depthFmt)
    {
        if (g_ssaaColor && g_ssaaDepth && g_ssaaDevice == dev && g_ssaaNativeW == nativeW && g_ssaaNativeH == nativeH)
            return true;
        SsaaReleaseTargets();
        const UINT w = (UINT)(nativeW * factor + 0.5f);
        const UINT h = (UINT)(nativeH * factor + 0.5f);
        if (FAILED(dev->CreateRenderTarget(w, h, colorFmt, D3DMULTISAMPLE_NONE, 0, FALSE, &g_ssaaColor, nullptr)))
        {
            WLOG_WARN("ssaa: CreateRenderTarget %ux%u fmt=%d failed", w, h, (int)colorFmt);
            SsaaReleaseTargets();
            return false;
        }
        if (FAILED(dev->CreateDepthStencilSurface(w, h, depthFmt, D3DMULTISAMPLE_NONE, 0, FALSE, &g_ssaaDepth, nullptr)))
        {
            WLOG_WARN("ssaa: CreateDepthStencilSurface %ux%u fmt=%d failed", w, h, (int)depthFmt);
            SsaaReleaseTargets();
            return false;
        }
        g_ssaaDevice = dev;
        g_ssaaNativeW = nativeW; g_ssaaNativeH = nativeH;
        g_ssaa2xW = w; g_ssaa2xH = h;
        WLOG_INFO("ssaa: offscreen world target %ux%u (native %ux%u) color=%d depth=%d", w, h, nativeW, nativeH, (int)colorFmt, (int)depthFmt);
        return true;
    }

    /** @brief Installs (or re-installs after a device recreate) the SetRenderTarget/SetDepthStencil filters. */
    void SsaaEnsureFilters(IDirect3DDevice9* dev);   // defined after SwapVtbl

    /**
     * @brief Frame start (called from Present): arms the redirect for the coming frame's world when
     *        supersampling is on, after making sure the filters and offscreen surfaces are ready. The world
     *        renders before the world-finalize boundary, so the redirect must already be armed here.
     */
    void SsaaArmFrame()
    {
        if (WxlGetSsaaFactor() <= 1.01f) { g_ssaaRedirect = false; return; }
        IDirect3DDevice9* dev = static_cast<IDirect3DDevice9*>(gx::RawDevice());
        uint8_t* base = GxBase();
        if (!dev || !base) { g_ssaaRedirect = false; return; }
        SsaaEnsureFilters(dev);

        IDirect3DSurface9* bb = nullptr;
        if (FAILED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) || !bb) { g_ssaaRedirect = false; return; }
        D3DSURFACE_DESC cd{};
        bb->GetDesc(&cd);
        bb->Release();

        D3DFORMAT depthFmt = D3DFMT_D24S8;
        if (IDirect3DSurface9* nd = *reinterpret_cast<IDirect3DSurface9**>(base + off::kDepthSurfaceField))
        {
            D3DSURFACE_DESC dd{};
            if (SUCCEEDED(nd->GetDesc(&dd))) depthFmt = dd.Format;
        }
        g_ssaaRedirect = SsaaEnsureTargets(dev, cd.Width, cd.Height, WxlGetSsaaFactor(), cd.Format, depthFmt);
    }

    /**
     * @brief Detours world-frame finalize: the world -> UI boundary. When the redirect was armed this frame
     *        the world has rendered into the offscreen surfaces; here the redirect is disarmed, the native
     *        render target / depth / curWindow restored for the UI, and OnWorldRenderEnd fired so the
     *        post-process pass downsamples the offscreen world into the native backbuffer.
     * @param worldFrame  world frame being finalized.
     */
    void __cdecl hkWorldFinalize(void* worldFrame)
    {
        // Keep the filters installed on the live device even if the vtable-based Present hook (which arms the
        // redirect) was dropped by a gxRestart device recreate; this minhook entry survives the recreate.
        if (WxlGetSsaaFactor() > 1.01f)
            if (IDirect3DDevice9* dev = static_cast<IDirect3DDevice9*>(gx::RawDevice()))
                SsaaEnsureFilters(dev);

        g_origWorldFinalize(worldFrame);

        const bool ssaa = g_ssaaRedirect;
        if (ssaa)
        {
            g_ssaaRedirect = false;   // UI binds from here on pass through to the native backbuffer
            IDirect3DDevice9* dev  = static_cast<IDirect3DDevice9*>(gx::RawDevice());
            uint8_t*          base = GxBase();
            if (dev)
            {
                IDirect3DSurface9* bb = nullptr;
                if (SUCCEEDED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb)
                {
                    g_origSetRT(dev, 0, bb);
                    bb->Release();
                }
                if (base)
                    if (IDirect3DSurface9* nd = *reinterpret_cast<IDirect3DSurface9**>(base + off::kDepthSurfaceField))
                        g_origSetDS(dev, nd);
            }
            if (base)
            {
                *reinterpret_cast<float*>(base + off::kCurWindowWidth)  = (float)g_ssaaNativeW;
                *reinterpret_cast<float*>(base + off::kCurWindowHeight) = (float)g_ssaaNativeH;
                *reinterpret_cast<int*>(base + off::kViewportDirty)     = 1;
            }
        }

        // The OnWorldRenderEnd subscriber downsamples the offscreen world surface into the native backbuffer
        // through On12 (D3D9 StretchRect to the backbuffer is rejected), then runs the post-process effects.
        ev::WorldRenderEndArgs a{ gx::RawDevice(),
                                  ssaa ? static_cast<void*>(g_ssaaColor) : nullptr,
                                  ssaa ? WxlGetSsaaFactor() : 1.0f };
        ev::Emit(ev::Event::OnWorldRenderEnd, &a);
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

    // Installs the SetRenderTarget/SetDepthStencil filters on the live device's vtable. Each device instance
    // has its own vtable, so on a gxRestart (new device) the swaps are gone; this re-applies them on the
    // current device. Idempotent per device via g_filterDevice. The world-render minhook (address-based)
    // survives the recreate and calls this each frame while supersampling is on.
    void SsaaEnsureFilters(IDirect3DDevice9* dev)
    {
        if (!dev || g_filterDevice == dev) return;
        void** vtbl = *reinterpret_cast<void***>(dev);
        SwapVtbl(vtbl, off::vt::kSetRenderTarget, reinterpret_cast<void*>(&hkSetRenderTarget), reinterpret_cast<void**>(&g_origSetRT));
        SwapVtbl(vtbl, off::vt::kSetDepthStencil, reinterpret_cast<void*>(&hkSetDepthStencil), reinterpret_cast<void**>(&g_origSetDS));
        g_filterDevice = dev;
        WLOG_INFO("ssaa: SetRenderTarget/SetDepthStencil filters installed (dev=%p)", (void*)dev);
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
        void** vtbl = *reinterpret_cast<void***>(dev);

        SwapVtbl(vtbl, off::vt::kDrawIndexedPrimitive, reinterpret_cast<void*>(&hkDIP),
                 reinterpret_cast<void**>(&g_origDIP));
        SwapVtbl(vtbl, off::vt::kEndScene, reinterpret_cast<void*>(&hkEndScene),
                 reinterpret_cast<void**>(&g_origEndScene));
        SwapVtbl(vtbl, off::vt::kPresent, reinterpret_cast<void*>(&hkPresent),
                 reinterpret_cast<void**>(&g_origPresent));

        // Function-entry detours; enabled by the batch EnableAll() the caller runs after all installers.
        wxl::core::hook::Install("M2DrawBatch", off::kDrawTriangleBatch,
                                 reinterpret_cast<void*>(&hkDrawBatch),
                                 reinterpret_cast<void**>(&g_origDrawBatch));
        wxl::core::hook::Install("WorldRenderFinalize", off::kWorldRenderFinalize,
                                 reinterpret_cast<void*>(&hkWorldFinalize),
                                 reinterpret_cast<void**>(&g_origWorldFinalize));
        wxl::core::hook::Install("M2RibbonDraw", m2off::kRibbonDraw,
                                 reinterpret_cast<void*>(&hkRibbonDraw),
                                 reinterpret_cast<void**>(&g_origRibbonDraw));
        wxl::core::hook::Install("LiquidRenderPass", off::kLiquidRenderPass,
                                 reinterpret_cast<void*>(&hkLiquidRender),
                                 reinterpret_cast<void**>(&g_origLiquidRender));

        WLOG_INFO("render: hooks installed (DIP, EndScene, Present, DrawBatch, WorldFinalize, RibbonDraw, LiquidRenderPass)");
    }
}
