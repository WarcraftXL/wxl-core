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
        g_curModel = *reinterpret_cast<void**>(
            reinterpret_cast<char*>(ctx) + off::kDrawBatchCtxModelField);
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
        return g_origPresent(dev, src, dst, wnd, dirty);
    }

    /**
     * @brief Detours world-frame finalize, emitting OnWorldRenderEnd at the world -> UI boundary.
     *
     * Runs after the native finalize, when the 3D scene is done and the UI pass has not started.
     * @param worldFrame  world frame being finalized.
     */
    void __cdecl hkWorldFinalize(void* worldFrame)
    {
        g_origWorldFinalize(worldFrame);
        ev::WorldRenderEndArgs a{ gx::RawDevice() };
        ev::Emit(ev::Event::OnWorldRenderEnd, &a);
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
        const void* const* arr = *reinterpret_cast<const void* const* const*>(emitter + m2off::kOffRibbonTexHandlePtr);
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
                layerCountPtr = reinterpret_cast<uint32_t*>(emitter + m2off::kOffRibbonLayerCount);
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

        WLOG_INFO("render: hooks installed (DIP, EndScene, Present, DrawBatch, WorldFinalize, RibbonDraw)");
    }
}
