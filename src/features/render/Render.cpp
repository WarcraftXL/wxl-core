// Native D3D9 render feature: device-vtable + engine detours that publish the render events.
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

#include "config.hpp"
#include "common/Mem.hpp"
#include "common/Log.hpp"
#include "engine/hook/Hook.hpp"
#include "engine/hook/Registry.hpp"
#include "engine/events/Event.hpp"
#include "game/gx/Gx.hpp"
#include "offsets/engine/Gx.hpp"

#include <windows.h>
#include <d3d9.h>

// Uses device-vtable pointer swaps (DrawIndexedPrimitive, EndScene, Present, Reset) plus function-entry
// hooks on the M2/liquid/world-boundary paths; no mid-function inline patch, so the native render pass
// stays intact. Each detour only publishes a render event -- the client owns its native D3D9 rendering.
namespace
{
    namespace off = wxl::offsets::engine::gx;
    namespace ev  = wxl::events;
    namespace gx  = wxl::game::gx;
    namespace rd  = wxl::features::render::detail;

    using EndSceneFn = long (__stdcall*)(void*);
    using PresentFn  = long (__stdcall*)(void*, const void*, const void*, void*, const void*);
    using ResetFn    = long (__stdcall*)(void*, D3DPRESENT_PARAMETERS*);

    EndSceneFn g_origEndScene = nullptr;
    PresentFn  g_origPresent  = nullptr;
    ResetFn    g_origReset    = nullptr;
    off::WorldRenderFinalizeFn g_origWorldFinalize = nullptr;
    off::LiquidRenderPassFn    g_origLiquidRender  = nullptr;
    void*      g_hookedDevice  = nullptr;   // device whose vtable currently carries the render hooks

    void EnsureDeviceHooks(IDirect3DDevice9* dev);   // defined after SwapVtbl

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
     * @brief Detours the per-frame liquid render pass, emitting OnLiquidRender before the native draw.
     *
     * Fires once per pass (passType 0 main, 1 secondary). The liquid textures are already bound and the
     * wave animation already applied at this point.
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

    /**
     * @brief Detours world-frame finalize: the world -> UI boundary. Re-applies the device vtable hooks on
     *        the live device (they are lost on a device recreate, and this function-entry hook survives it),
     *        runs the native finalize, then emits OnWorldRenderEnd for post-world subscribers.
     * @param worldFrame  world frame being finalized.
     */
    void __cdecl hkWorldFinalize(void* worldFrame)
    {
        if (IDirect3DDevice9* d = static_cast<IDirect3DDevice9*>(gx::RawDevice()))
            EnsureDeviceHooks(d);

        g_origWorldFinalize(worldFrame);

        ev::WorldRenderEndArgs a{ gx::RawDevice() };
        ev::Emit(ev::Event::OnWorldRenderEnd, &a);
    }

    /**
     * @brief Replaces one vtable entry with a hook, returning the original through origOut.
     * @param vtbl     vtable base.
     * @param idx      entry index to swap.
     * @param hook     replacement function pointer.
     * @param origOut  receives the original entry.
     */
    template <class Fn>
    void SwapVtbl(void** vtbl, unsigned idx, Fn* hook, Fn** origOut)
    {
        wxl::mem::SwapPointer(&vtbl[idx], reinterpret_cast<void*>(hook),
                              reinterpret_cast<void**>(origOut));
    }

    /**
     * @brief Detours IDirect3DDevice9::Reset (a resolution / window resize). D3D9 requires every
     *        D3DPOOL_DEFAULT resource released before a Reset or it fails (D3DERR_INVALIDCALL) and the device
     *        is lost -- the resize "crash". Fires OnDeviceLost so subscribers retire their GPU work and free
     *        DEFAULT-pool resources, releases the engine's own tracked render targets, runs the native Reset,
     *        then fires OnDeviceReset on success so subscribers recreate.
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
        // Subscribers using engine-owned color/depth surfaces must retire their GPU work and release their
        // DEFAULT-pool resources before the native Reset, or the Reset fails and the device is lost.
        ev::Emit(ev::Event::OnDeviceLost, &a);
        gx::ReleaseResetResources();  // free any tracked engine render targets (DEFAULT pool)

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

    /**
     * @brief Installs the render vtable hooks on the live device. Each device instance may carry its own
     *        vtable, so on a device recreate the swaps are gone; this re-applies them on the current device.
     *        The surviving world-render function-entry hook calls this each frame, so OnEndScene / OnFrame /
     *        OnM2BatchDraw keep firing after a graphics restart. Guarded against a shared vtable: if it already
     *        carries our hooks, re-swapping would capture our own hook as the "original" and recurse, so only
     *        the device pointer is updated.
     * @param dev  the live D3D9 device.
     */
    void EnsureDeviceHooks(IDirect3DDevice9* dev)
    {
        if (!dev || g_hookedDevice == dev) return;
        void** vtbl = *reinterpret_cast<void***>(dev);
        if (vtbl[off::vt::kPresent] != reinterpret_cast<void*>(&hkPresent))
        {
            SwapVtbl(vtbl, off::vt::kDrawIndexedPrimitive, &rd::hkDIP, &rd::g_origDIP);
            SwapVtbl(vtbl, off::vt::kEndScene,             &hkEndScene, &g_origEndScene);
            SwapVtbl(vtbl, off::vt::kPresent,              &hkPresent, &g_origPresent);
            SwapVtbl(vtbl, off::vt::kReset,                &hkReset, &g_origReset);
            WLOG_INFO("render: device hooks installed (dev=%p)", (void*)dev);
        }
        g_hookedDevice = dev;
    }

    /**
     * @brief Installs the render detours: device vtable swaps plus the M2 / world-boundary / liquid
     *        function-entry hooks. Detours are enabled by the caller's batch EnableAll() afterwards.
     * @return true; a missing device only defers the vtable swaps to the first world finalize.
     */
    bool Install()
    {
        if (void* dev = gx::RawDevice())
            EnsureDeviceHooks(static_cast<IDirect3DDevice9*>(dev));
        else
            WLOG_WARN("render: device not up, vtable hooks deferred to first world finalize");

        rd::InstallM2DrawHooks();
        wxl::hook::Install("WorldRenderFinalize", off::kWorldRenderFinalize,
                           &hkWorldFinalize, &g_origWorldFinalize);
        wxl::hook::Install("LiquidRenderPass", off::kLiquidRenderPass,
                           &hkLiquidRender, &g_origLiquidRender);

        WLOG_INFO("render: hooks installed (DIP, EndScene, Present, Reset, DrawBatch, WorldFinalize, RibbonDraw, LiquidRenderPass)");
        return true;
    }
}

WXL_REGISTER_FEATURE("render", wxl::features::kRender, Install)
