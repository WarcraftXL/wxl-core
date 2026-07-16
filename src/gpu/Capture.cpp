// Captures the engine's D3D9 device by intercepting IDirect3D9::CreateDevice.
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

#include "gpu/Capture.hpp"
#include "gpu/Device.hpp"
#include "gpu/Present.hpp"

#include <windows.h>

namespace
{
    using wxl::gpu::Log;

    IDirect3DDevice9*           g_device       = nullptr;
    ID3D12CommandQueue*         g_presentQueue = nullptr;
    HWND                        g_devWindow    = nullptr;
    BOOL                        g_windowed     = TRUE;
    float                       g_ssaaFactor   = 1.0f;   // supersampling: backbuffer scale (1.0 = off)
    wxl::gpu::capture::FrameFn  g_frame        = nullptr;

    // Standard IDirect3DDevice9 vtable indices (Reset=16, Present=17, BeginScene=41, EndScene=42, Clear=43).
    constexpr int kVtReset    = 16;
    constexpr int kVtPresent  = 17;
    constexpr int kVtEndScene = 42;
    using ResetFn    = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
    using EndSceneFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*);
    using PresentFn  = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
    ResetFn    g_origReset     = nullptr;
    EndSceneFn g_origEndScene  = nullptr;
    PresentFn  g_origPresent   = nullptr;
    UINT       g_customPresentMisses = 0;

    HRESULT STDMETHODCALLTYPE HookReset(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* pp);

    /**
     * @brief Invokes the per-frame callback then forwards to the original EndScene.
     * @param dev  device whose EndScene was called.
     * @return Result of the original EndScene.
     */
    HRESULT STDMETHODCALLTYPE HookEndScene(IDirect3DDevice9* dev)
    {
        if (g_frame) g_frame(dev);
        return g_origEndScene(dev);
    }

    /**
     * @brief Logs the present args + HRESULT for the first frames (windowed present diagnostics).
     * @param dev    presenting device.
     * @param src    source rect.
     * @param dst    destination rect.
     * @param wnd    destination window override (windowed present).
     * @param dirty  dirty region.
     * @return Result of the original Present.
     */
    HRESULT STDMETHODCALLTYPE HookPresent(IDirect3DDevice9* dev, const RECT* src, const RECT* dst, HWND wnd, const RGNDATA* dirty)
    {
        // The d3d9on12 windowed (DWM) present does not show the backbuffer, so own the present in windowed
        // mode: show the frame through our own swapchain and skip the native present. Exclusive fullscreen is
        // normally translated to borderless-windowed at device creation to avoid unstable On12 driver resets;
        // only the explicit compatibility opt-out keeps the native fullscreen present path.
        if (g_windowed)
        {
            HWND target = wnd ? wnd : g_devWindow;
            if (target && wxl::gpu::present::Present(dev, target))
            {
                g_customPresentMisses = 0;
                return S_OK;
            }

            // This proxy owns windowed presentation, so calling the D3D9On12 Present underneath it is
            // unsafe. In particular, glue-screen/reset transitions can make the custom presenter miss one
            // frame while resources are being replaced; falling through here then enters D3D9On12 with a
            // backbuffer whose underlying resource was managed by our queue and can crash inside d3d9on12.
            // Keep DWM's last completed frame and retry on the next engine Present instead.
            ++g_customPresentMisses;
            if (g_customPresentMisses <= 4 || (g_customPresentMisses % 600) == 0)
                Log("capture: custom present miss %u; native windowed Present suppressed", g_customPresentMisses);
            return S_OK;
        }
        return g_origPresent(dev, src, dst, wnd, dirty);
    }

    /**
     * @brief Patches this device instance's Present + EndScene vtable slots to the hooks.
     * @param dev  device whose vtable is patched; On12 gives each device its own vtable.
     */
    void PatchEndScene(IDirect3DDevice9* dev)
    {
        void** vt = *reinterpret_cast<void***>(dev);
        DWORD old = 0;
        VirtualProtect(&vt[kVtEndScene], sizeof(void*), PAGE_EXECUTE_READWRITE, &old);
        g_origEndScene = reinterpret_cast<EndSceneFn>(vt[kVtEndScene]);
        vt[kVtEndScene] = reinterpret_cast<void*>(&HookEndScene);
        VirtualProtect(&vt[kVtEndScene], sizeof(void*), old, &old);

        VirtualProtect(&vt[kVtPresent], sizeof(void*), PAGE_EXECUTE_READWRITE, &old);
        g_origPresent = reinterpret_cast<PresentFn>(vt[kVtPresent]);
        vt[kVtPresent] = reinterpret_cast<void*>(&HookPresent);
        VirtualProtect(&vt[kVtPresent], sizeof(void*), old, &old);

        VirtualProtect(&vt[kVtReset], sizeof(void*), PAGE_EXECUTE_READWRITE, &old);
        g_origReset = reinterpret_cast<ResetFn>(vt[kVtReset]);
        vt[kVtReset] = reinterpret_cast<void*>(&HookReset);
        VirtualProtect(&vt[kVtReset], sizeof(void*), old, &old);
    }

    /**
     * @brief Logs the present parameters a device was created with (windowed present diagnostics).
     * @param pp  present parameters, may be null.
     */
    void LogPresentParams(const D3DPRESENT_PARAMETERS* pp)
    {
        if (!pp) { Log("capture: CreateDevice pp=null"); return; }
        Log("capture: pp Windowed=%d SwapEffect=%d BBCount=%u %ux%u fmt=%d hDevWnd=%p Flags=0x%X PresentInterval=0x%X",
            pp->Windowed, (int)pp->SwapEffect, pp->BackBufferCount,
            pp->BackBufferWidth, pp->BackBufferHeight, (int)pp->BackBufferFormat,
            pp->hDeviceWindow, pp->Flags, pp->PresentationInterval);
    }

    /** @brief True only when the user explicitly opts back into native D3D9On12 exclusive fullscreen. */
    bool AllowExclusiveFullscreen()
    {
        static const bool allowed = []() {
            char value[16] = {};
            const DWORD n = GetEnvironmentVariableA("WXL_EXCLUSIVE_FULLSCREEN", value, sizeof(value));
            if (n && n < sizeof(value))
            {
                const char c = value[0];
                if (c != '0' && c != 'n' && c != 'N' && c != 'f' && c != 'F')
                    return true;
            }
            return GetFileAttributesA("WarcraftXLExclusiveFullscreen.flag") != INVALID_FILE_ATTRIBUTES;
        }();
        return allowed;
    }

    /**
     * @brief Makes windowed present params compatible with the flip-model swapchain On12 must use.
     *
     * On12 bridges the D3D9 present to a DXGI flip-model swapchain. Flip-model forbids a lockable backbuffer
     * and needs at least two buffers, so a windowed device asking for a lockable single-buffer chain (which
     * the engine does) presents to a surface DWM never composites, leaving the window white. Clearing the
     * lockable flag and raising the buffer count to two lets the windowed flip-model present reach DWM.
     * Native exclusive fullscreen is left untouched here; PrepareNativePresentParams decides whether the
     * compatibility opt-out permits it before applying this windowed sanitize.
     * @param pp  present parameters to sanitize in place, may be null.
     */
    void SanitizePresentParams(D3DPRESENT_PARAMETERS* pp)
    {
        if (!pp || !pp->Windowed) return;
        const DWORD before = pp->Flags;
        const UINT  beforeCount = pp->BackBufferCount;
        pp->Flags &= ~D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
        if (pp->BackBufferCount < 2) pp->BackBufferCount = 2;
        if (pp->Flags != before || pp->BackBufferCount != beforeCount)
            Log("capture: windowed present sanitized (Flags 0x%X->0x%X, BBCount %u->%u)",
                before, pp->Flags, beforeCount, pp->BackBufferCount);

        // Supersampling is NOT done by enlarging the backbuffer: the windowed On12 backbuffer is pinned to
        // the window client size (an inflated BackBufferWidth is silently clamped), so the engine would
        // render the world clipped to a corner. Instead the world pass is redirected into an offscreen 2x
        // surface inside the world-render detour and downsampled into this native backbuffer (see
        // runtime/RenderHooks.cpp). The backbuffer stays at native (window) resolution here.
    }

    /**
     * @brief Builds parameters for the native On12 device while leaving WoW's requested mode unchanged.
     *
     * NVIDIA driver 576.80 repeatedly null-dereferences inside D3D9On12 while resetting an exclusive
     * fullscreen device. Borderless-windowed retains the same fullscreen window and desktop refresh rate but
     * avoids that driver path; the proxy's composition presenter already owns windowed output.
     */
    void PrepareNativePresentParams(D3DPRESENT_PARAMETERS* pp)
    {
        if (!pp) return;
        if (!pp->Windowed && !AllowExclusiveFullscreen())
        {
            Log("capture: exclusive fullscreen %ux%u@%u redirected to borderless-windowed for stable On12 reset",
                pp->BackBufferWidth, pp->BackBufferHeight, pp->FullScreen_RefreshRateInHz);
            pp->Windowed = TRUE;
            pp->FullScreen_RefreshRateInHz = 0;
        }
        SanitizePresentParams(pp);
    }

    /**
     * @brief Re-sanitizes present params on a device reset (resolution change, mode switch).
     *
     * The engine resets the device (it does not recreate it) when the resolution or window mode changes, so
     * the windowed present sanitize (flip-model backbuffer count, no lockable flag) must be reapplied here,
     * not only at CreateDevice.
     * @param dev  device being reset.
     * @param pp   new present parameters, sanitized in place.
     * @return Result of the original Reset.
     */
    HRESULT STDMETHODCALLTYPE HookReset(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* pp)
    {
        if (!pp) return g_origReset(dev, pp);

        Log("capture: Reset begin %ux%u windowed=%d BBCount=%u",
            pp->BackBufferWidth, pp->BackBufferHeight, pp->Windowed, pp->BackBufferCount);
        if (!wxl::gpu::present::PrepareForReset())
        {
            Log("capture: Reset deferred because proxy GPU work did not drain");
            return D3DERR_DEVICELOST;
        }

        // Do not rewrite WoW's persistent presentation-parameter block. The engine compares that
        // state later and repeatedly resets the device if our On12-only BBCount/Flags changes leak
        // back into it, producing a brief hide/flicker loop during loading. Sanitize a call copy.
        D3DPRESENT_PARAMETERS sanitized = *pp;
        PrepareNativePresentParams(&sanitized);
        g_windowed = sanitized.Windowed;
        if (sanitized.hDeviceWindow) g_devWindow = sanitized.hDeviceWindow;
        const HRESULT hr = g_origReset(dev, &sanitized);
        Log("capture: Reset end hr=0x%08lX", (unsigned long)hr);
        return hr;
    }

    /**
     * @brief Records the engine device + its On12 queue on first capture and hooks its EndScene.
     * @param dev    engine device to capture.
     * @param queue  the On12 queue the device's factory runs on.
     */
    void Capture(IDirect3DDevice9* dev, ID3D12CommandQueue* queue)
    {
        if (g_device == dev) return;   // already hooked this device
        // The engine recreates the device (and its window) on a graphics restart (e.g. /console gxRestart),
        // giving it a fresh vtable, so re-hook the new device instead of leaving it unhooked (white screen).
        g_device = dev;
        g_presentQueue = queue;
        PatchEndScene(dev);
        Log("capture: engine device %p captured (queue %p), hooks installed", dev, queue);
    }

    /**
     * @brief Forwarding wrapper over the real factory that intercepts only CreateDevice and CreateDeviceEx.
     *
     * Every other call passes straight through. realEx_ is null when the engine used the non-Ex create path.
     */
    class WrappedD3D9 : public IDirect3D9Ex
    {
    public:
        /**
         * @brief Wraps a real factory, optionally with its Ex interface.
         * @param real    real IDirect3D9 factory.
         * @param realEx  real IDirect3D9Ex factory, or null on the non-Ex path.
         * @param queue   the On12 queue this factory runs on, recorded if it creates the captured device.
         */
        explicit WrappedD3D9(IDirect3D9* real, IDirect3D9Ex* realEx, ID3D12CommandQueue* queue)
            : real_(real), realEx_(realEx), queue_(queue) {}

        // --- IUnknown ---
        /**
         * @brief Returns this wrapper for the factory interfaces, otherwise forwards the query.
         * @param riid  requested interface id.
         * @param ppv   receives the interface pointer.
         * @return S_OK when handled here, otherwise the real factory's result.
         */
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
        {
            if (riid == __uuidof(IDirect3D9) || riid == __uuidof(IUnknown)
                || (realEx_ && riid == __uuidof(IDirect3D9Ex)))
            {
                AddRef();
                *ppv = this;
                return S_OK;
            }
            return real_->QueryInterface(riid, ppv);
        }
        /** @brief Increments the reference count. @return The new count. */
        ULONG STDMETHODCALLTYPE AddRef() override { return ++ref_; }

        /**
         * @brief Decrements the reference count, releasing the real factory and freeing this wrapper at zero.
         * @return The new count.
         */
        ULONG STDMETHODCALLTYPE Release() override
        {
            ULONG r = --ref_;
            if (r == 0) { real_->Release(); delete this; }
            return r;
        }

        // --- IDirect3D9 ---
        HRESULT STDMETHODCALLTYPE RegisterSoftwareDevice(void* p) override { return real_->RegisterSoftwareDevice(p); }
        UINT STDMETHODCALLTYPE GetAdapterCount() override { return real_->GetAdapterCount(); }
        HRESULT STDMETHODCALLTYPE GetAdapterIdentifier(UINT a, DWORD f, D3DADAPTER_IDENTIFIER9* id) override { return real_->GetAdapterIdentifier(a, f, id); }
        UINT STDMETHODCALLTYPE GetAdapterModeCount(UINT a, D3DFORMAT fmt) override { return real_->GetAdapterModeCount(a, fmt); }
        HRESULT STDMETHODCALLTYPE EnumAdapterModes(UINT a, D3DFORMAT fmt, UINT i, D3DDISPLAYMODE* m) override { return real_->EnumAdapterModes(a, fmt, i, m); }
        HRESULT STDMETHODCALLTYPE GetAdapterDisplayMode(UINT a, D3DDISPLAYMODE* m) override { return real_->GetAdapterDisplayMode(a, m); }
        HRESULT STDMETHODCALLTYPE CheckDeviceType(UINT a, D3DDEVTYPE t, D3DFORMAT df, D3DFORMAT bf, BOOL win) override { return real_->CheckDeviceType(a, t, df, bf, win); }
        HRESULT STDMETHODCALLTYPE CheckDeviceFormat(UINT a, D3DDEVTYPE t, D3DFORMAT af, DWORD u, D3DRESOURCETYPE rt, D3DFORMAT cf) override { return real_->CheckDeviceFormat(a, t, af, u, rt, cf); }
        HRESULT STDMETHODCALLTYPE CheckDeviceMultiSampleType(UINT a, D3DDEVTYPE t, D3DFORMAT sf, BOOL win, D3DMULTISAMPLE_TYPE mt, DWORD* q) override { return real_->CheckDeviceMultiSampleType(a, t, sf, win, mt, q); }
        HRESULT STDMETHODCALLTYPE CheckDepthStencilMatch(UINT a, D3DDEVTYPE t, D3DFORMAT af, D3DFORMAT rf, D3DFORMAT df) override { return real_->CheckDepthStencilMatch(a, t, af, rf, df); }
        HRESULT STDMETHODCALLTYPE CheckDeviceFormatConversion(UINT a, D3DDEVTYPE t, D3DFORMAT sf, D3DFORMAT tf) override { return real_->CheckDeviceFormatConversion(a, t, sf, tf); }
        HRESULT STDMETHODCALLTYPE GetDeviceCaps(UINT a, D3DDEVTYPE t, D3DCAPS9* c) override { return real_->GetDeviceCaps(a, t, c); }
        HMONITOR STDMETHODCALLTYPE GetAdapterMonitor(UINT a) override { return real_->GetAdapterMonitor(a); }
        /**
         * @brief Forwards CreateDevice and captures the resulting device.
         * @param a    adapter ordinal.
         * @param t    device type.
         * @param fw   focus window.
         * @param bf   behavior flags.
         * @param pp   present parameters.
         * @param ret  receives the created device.
         * @return Result of the real CreateDevice.
         */
        HRESULT STDMETHODCALLTYPE CreateDevice(UINT a, D3DDEVTYPE t, HWND fw, DWORD bf, D3DPRESENT_PARAMETERS* pp, IDirect3DDevice9** ret) override
        {
            LogPresentParams(pp);
            if (!pp) return real_->CreateDevice(a, t, fw, bf, pp, ret);
            D3DPRESENT_PARAMETERS native = *pp;
            PrepareNativePresentParams(&native);
            g_devWindow = native.hDeviceWindow ? native.hDeviceWindow : fw;
            g_windowed  = native.Windowed;
            HRESULT hr = real_->CreateDevice(a, t, fw, bf, &native, ret);
            if (SUCCEEDED(hr) && ret && *ret) Capture(*ret, queue_);
            return hr;
        }

        // --- IDirect3D9Ex ---
        UINT STDMETHODCALLTYPE GetAdapterModeCountEx(UINT a, const D3DDISPLAYMODEFILTER* f) override { return realEx_ ? realEx_->GetAdapterModeCountEx(a, f) : 0; }
        HRESULT STDMETHODCALLTYPE EnumAdapterModesEx(UINT a, const D3DDISPLAYMODEFILTER* f, UINT i, D3DDISPLAYMODEEX* m) override { return realEx_ ? realEx_->EnumAdapterModesEx(a, f, i, m) : E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE GetAdapterDisplayModeEx(UINT a, D3DDISPLAYMODEEX* m, D3DDISPLAYROTATION* r) override { return realEx_ ? realEx_->GetAdapterDisplayModeEx(a, m, r) : E_NOTIMPL; }
        /**
         * @brief Forwards CreateDeviceEx and captures the resulting device.
         * @param a    adapter ordinal.
         * @param t    device type.
         * @param fw   focus window.
         * @param bf   behavior flags.
         * @param pp   present parameters.
         * @param fsm  fullscreen display mode.
         * @param ret  receives the created device.
         * @return Result of the real CreateDeviceEx, or E_NOTIMPL without an Ex factory.
         */
        HRESULT STDMETHODCALLTYPE CreateDeviceEx(UINT a, D3DDEVTYPE t, HWND fw, DWORD bf, D3DPRESENT_PARAMETERS* pp, D3DDISPLAYMODEEX* fsm, IDirect3DDevice9Ex** ret) override
        {
            if (!realEx_) return E_NOTIMPL;
            LogPresentParams(pp);
            if (!pp) return realEx_->CreateDeviceEx(a, t, fw, bf, pp, fsm, ret);
            D3DPRESENT_PARAMETERS native = *pp;
            PrepareNativePresentParams(&native);
            g_devWindow = native.hDeviceWindow ? native.hDeviceWindow : fw;
            g_windowed  = native.Windowed;
            HRESULT hr = realEx_->CreateDeviceEx(a, t, fw, bf, &native, native.Windowed ? nullptr : fsm, ret);
            if (SUCCEEDED(hr) && ret && *ret) Capture(*ret, queue_);
            return hr;
        }
        HRESULT STDMETHODCALLTYPE GetAdapterLUID(UINT a, LUID* luid) override { return realEx_ ? realEx_->GetAdapterLUID(a, luid) : E_NOTIMPL; }

    private:
        IDirect3D9*         real_   = nullptr;
        IDirect3D9Ex*       realEx_ = nullptr;
        ID3D12CommandQueue* queue_  = nullptr;
        ULONG               ref_    = 1;
    };
}

namespace wxl::gpu::capture
{
    /** @brief Wraps a real factory for interception. @param real real factory. @param queue factory's On12 queue. @return Wrapper, or null if real is null. */
    IDirect3D9*   Wrap(IDirect3D9* real, ID3D12CommandQueue* queue)     { return real ? new WrappedD3D9(real, nullptr, queue) : nullptr; }

    /** @brief Wraps a real Ex factory for interception. @param real real Ex factory. @param queue factory's On12 queue. @return Wrapper, or null if real is null. */
    IDirect3D9Ex* WrapEx(IDirect3D9Ex* real, ID3D12CommandQueue* queue) { return real ? new WrappedD3D9(real, real, queue) : nullptr; }

    /** @brief Registers the per-frame callback. @param fn callback invoked at each EndScene. */
    void              OnFrame(FrameFn fn)    { g_frame = fn; }

    /** @brief Returns the captured engine device. @return The device, or null before capture. */
    IDirect3DDevice9* Device()               { return g_device; }

    /** @brief Returns the captured (presenting) device's On12 queue. @return The queue, or null before capture. */
    ID3D12CommandQueue* PresentQueue()       { return g_presentQueue; }

    /** @brief Sets the supersampling factor (applied on the next device create/reset). @param factor 1.0 = off. */
    void  SetSsaaFactor(float factor)        { g_ssaaFactor = factor; }

    /** @brief Returns the current supersampling factor. @return The factor (1.0 = off). */
    float SsaaFactor()                       { return g_ssaaFactor; }
}
