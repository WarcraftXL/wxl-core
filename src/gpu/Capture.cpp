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

#include <windows.h>

namespace
{
    using wxl::gpu::Log;

    IDirect3DDevice9*           g_device = nullptr;
    wxl::gpu::capture::FrameFn  g_frame  = nullptr;

    // Standard IDirect3DDevice9 vtable index of EndScene (BeginScene=41, EndScene=42, Clear=43).
    constexpr int kVtEndScene = 42;
    using EndSceneFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*);
    EndSceneFn g_origEndScene = nullptr;

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
     * @brief Patches this device instance's EndScene vtable slot to the hook.
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
    }

    /**
     * @brief Records the engine device on first capture and hooks its EndScene.
     * @param dev  engine device to capture.
     */
    void Capture(IDirect3DDevice9* dev)
    {
        if (g_device) return;
        g_device = dev;
        PatchEndScene(dev);
        Log("capture: engine device %p captured, EndScene hooked", dev);
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
         */
        explicit WrappedD3D9(IDirect3D9* real, IDirect3D9Ex* realEx)
            : real_(real), realEx_(realEx) {}

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
            HRESULT hr = real_->CreateDevice(a, t, fw, bf, pp, ret);
            if (SUCCEEDED(hr) && ret && *ret) Capture(*ret);
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
            HRESULT hr = realEx_->CreateDeviceEx(a, t, fw, bf, pp, fsm, ret);
            if (SUCCEEDED(hr) && ret && *ret) Capture(*ret);
            return hr;
        }
        HRESULT STDMETHODCALLTYPE GetAdapterLUID(UINT a, LUID* luid) override { return realEx_ ? realEx_->GetAdapterLUID(a, luid) : E_NOTIMPL; }

    private:
        IDirect3D9*   real_   = nullptr;
        IDirect3D9Ex* realEx_ = nullptr;
        ULONG         ref_    = 1;
    };
}

namespace wxl::gpu::capture
{
    /** @brief Wraps a real factory for interception. @param real real factory. @return Wrapper, or null if real is null. */
    IDirect3D9*   Wrap(IDirect3D9* real)     { return real ? new WrappedD3D9(real, nullptr) : nullptr; }

    /** @brief Wraps a real Ex factory for interception. @param real real Ex factory. @return Wrapper, or null if real is null. */
    IDirect3D9Ex* WrapEx(IDirect3D9Ex* real) { return real ? new WrappedD3D9(real, real) : nullptr; }

    /** @brief Registers the per-frame callback. @param fn callback invoked at each EndScene. */
    void              OnFrame(FrameFn fn)    { g_frame = fn; }

    /** @brief Returns the captured engine device. @return The device, or null before capture. */
    IDirect3DDevice9* Device()               { return g_device; }
}
