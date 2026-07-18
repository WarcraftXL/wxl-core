// M2 batch draw path: 32-bit start-index expansion, ribbon multi-texture fold, OnM2BatchDraw.
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

#include "features/render/Render.hpp"

#include "common/Log.hpp"
#include "engine/hook/Hook.hpp"
#include "engine/events/Event.hpp"
#include "engine/assets/m2/M2Format.hpp"
#include "offsets/engine/Gx.hpp"
#include "offsets/game/M2.hpp"

#include <windows.h>
#include <d3d9.h>

namespace
{
    namespace off   = wxl::offsets::engine::gx;
    namespace ev    = wxl::events;
    namespace m2off = wxl::offsets::game::m2;

    // The model currently drawing, captured between a batch-draw enter and its DrawIndexedPrimitive.
    void* g_curModel = nullptr;
    // The current M2 batch draw context; its copied skin-section pointer is populated before the DIP call.
    void* g_curDrawCtx = nullptr;
    // Re-entrancy guard: a subscriber re-issues the draw through the hooked vtable, so do not re-emit.
    bool  g_inM2Emit = false;

    using DrawBatchFn = void (__fastcall*)(void* ctx, void* edx);
    DrawBatchFn g_origDrawBatch = nullptr;

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

        long r = wxl::features::render::detail::g_origDIP(dev, pt, bv, mi, nv, si, pc);

        for (DWORD st = 0; st < 4; ++st)
        {
            dev->SetTextureStageState(st, D3DTSS_COLOROP,   s[st][0]);
            dev->SetTextureStageState(st, D3DTSS_COLORARG1, s[st][1]);
            dev->SetTextureStageState(st, D3DTSS_COLORARG2, s[st][2]);
            dev->SetTextureStageState(st, D3DTSS_ALPHAOP,   s[st][3]);
        }
        return r;
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
}

namespace wxl::features::render::detail
{
    DIPFn g_origDIP = nullptr;

    /**
     * @brief DrawIndexedPrimitive detour: folds multi-texture ribbons and emits OnM2BatchDraw.
     *
     * A multi-texture ribbon pass is folded into one combine; otherwise the native draw runs (with the
     * 32-bit M2 start index restored) and the M2 batch is published with its draw parameters. Guarded so
     * a subscriber re-issue does not recurse.
     */
    long __stdcall hkDIP(void* dev, int pt, int bv, unsigned mi, unsigned nv, unsigned si, unsigned pc)
    {
        if (g_ribbonModern)
            return DrawRibbonMultiTexture(static_cast<IDirect3DDevice9*>(dev), pt, bv, mi, nv, si, pc);

        const unsigned drawStartIndex = ExpandM2StartIndex(si);
        long r = g_origDIP(dev, pt, bv, mi, nv, drawStartIndex, pc);
        // Building the args and crossing into Emit costs real time at per-batch frequency; skip the
        // whole emission while nothing subscribes.
        if (g_curModel && !g_inM2Emit && ev::Any(ev::Event::OnM2BatchDraw))
        {
            g_inM2Emit = true;
            ev::M2BatchDrawArgs a{ dev, g_curModel, pt, bv, mi, nv, drawStartIndex, pc };
            ev::Emit(ev::Event::OnM2BatchDraw, &a);
            g_inM2Emit = false;
        }
        return r;
    }

    /** @brief Installs the M2 batch-draw and ribbon-draw function-entry detours. */
    void InstallM2DrawHooks()
    {
        wxl::hook::Install("M2DrawBatch", off::kDrawTriangleBatch,
                           &hkDrawBatch, &g_origDrawBatch);
        wxl::hook::Install("M2RibbonDraw", m2off::kRibbonDraw,
                           &hkRibbonDraw, &g_origRibbonDraw);
    }
}
