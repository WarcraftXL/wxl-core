// The d3d9.dll proxy: run the engine on D3D12 via On12, capture its device, own the D3D12 device+queue.
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

// The client LoadLibrary's "d3d9.dll" from its own folder first, so this proxy loads ahead of the system one.

#include <windows.h>
#include <d3d9.h>

#include "gpu/Device.hpp"
#include "gpu/Capture.hpp"

using wxl::gpu::Log;

namespace
{
    using Create9Fn       = IDirect3D9* (WINAPI*)(UINT);
    using Create9ExFn     = HRESULT     (WINAPI*)(UINT, IDirect3D9Ex**);
    using Create9On12Fn   = IDirect3D9* (WINAPI*)(UINT, D3D9ON12_ARGS*, UINT);
    using Create9On12ExFn = HRESULT     (WINAPI*)(UINT, D3D9ON12_ARGS*, UINT, IDirect3D9Ex**);
    Create9Fn       g_realCreate9       = nullptr;
    Create9ExFn     g_realCreate9Ex     = nullptr;
    Create9On12Fn   g_realCreate9On12   = nullptr;
    Create9On12ExFn g_realCreate9On12Ex = nullptr;

    /**
     * @brief Loads the real d3d9 from the system directory, falling back to a local d3d9_real.dll.
     *
     * A loaded module is keyed by full path, so the system d3d9.dll is a distinct module from this proxy
     * despite the shared base name.
     * @return Handle to the real d3d9 module, or null on failure.
     */
    HMODULE LoadRealD3D9()
    {
        char path[MAX_PATH];
        UINT n = GetSystemDirectoryA(path, MAX_PATH);
        if (n != 0 && n < MAX_PATH - 16)
        {
            lstrcatA(path, "\\d3d9.dll");
            if (HMODULE r = LoadLibraryA(path)) return r;
        }
        return LoadLibraryA("d3d9_real.dll");
    }

    /** @brief Lazily loads the real d3d9 and resolves its create entry points on first use. */
    void EnsureReal()
    {
        if (g_realCreate9) return;
        HMODULE r = LoadRealD3D9();
        if (!r) { Log("d3d9proxy: FAILED to load the system d3d9.dll"); return; }
        g_realCreate9       = reinterpret_cast<Create9Fn>(GetProcAddress(r, "Direct3DCreate9"));
        g_realCreate9Ex     = reinterpret_cast<Create9ExFn>(GetProcAddress(r, "Direct3DCreate9Ex"));
        g_realCreate9On12   = reinterpret_cast<Create9On12Fn>(GetProcAddress(r, "Direct3DCreate9On12"));
        g_realCreate9On12Ex = reinterpret_cast<Create9On12ExFn>(GetProcAddress(r, "Direct3DCreate9On12Ex"));
        Log("d3d9proxy: system d3d9 loaded (9=%p Ex=%p On12=%p On12Ex=%p)",
            g_realCreate9, g_realCreate9Ex, g_realCreate9On12, g_realCreate9On12Ex);
    }
}

/**
 * @brief Proxy entry point for Direct3DCreate9 that routes through On12 and wraps the factory.
 * @param sdkVersion  D3D SDK version passed by the caller.
 * @return Wrapped IDirect3D9 on the On12 path, the native factory on fallback, or null on failure.
 */
extern "C" IDirect3D9* WINAPI Direct3DCreate9(UINT sdkVersion)
{
    EnsureReal();
    if (g_realCreate9On12)
    {
        D3D9ON12_ARGS args = wxl::gpu::MakeOn12Args();
        IDirect3D9* d3d = g_realCreate9On12(sdkVersion, &args, 1);
        Log("d3d9proxy: Direct3DCreate9On12 -> %p (D3D12 %p queue %p)", d3d, args.pD3D12Device, args.ppD3D12Queues[0]);
        if (d3d) return wxl::gpu::capture::Wrap(d3d);   // intercept CreateDevice to capture the device
    }
    Log("d3d9proxy: native Direct3DCreate9 fallback");
    return g_realCreate9 ? g_realCreate9(sdkVersion) : nullptr;
}

/**
 * @brief Proxy entry point for Direct3DCreate9Ex that routes through On12 and wraps the factory.
 * @param sdkVersion  D3D SDK version passed by the caller.
 * @param out         receives the wrapped IDirect3D9Ex.
 * @return S_OK on success, or the fallback create result.
 */
extern "C" HRESULT WINAPI Direct3DCreate9Ex(UINT sdkVersion, IDirect3D9Ex** out)
{
    EnsureReal();
    if (g_realCreate9On12Ex)
    {
        D3D9ON12_ARGS args = wxl::gpu::MakeOn12Args();
        HRESULT hr = g_realCreate9On12Ex(sdkVersion, &args, 1, out);
        Log("d3d9proxy: Direct3DCreate9On12Ex -> hr=0x%08lx (D3D12 %p queue %p)", hr, args.pD3D12Device, args.ppD3D12Queues[0]);
        if (SUCCEEDED(hr))
        {
            if (out && *out) *out = wxl::gpu::capture::WrapEx(*out);   // intercept CreateDeviceEx
            return hr;
        }
    }
    Log("d3d9proxy: native Direct3DCreate9Ex fallback");
    return g_realCreate9Ex ? g_realCreate9Ex(sdkVersion, out) : E_NOINTERFACE;
}

/**
 * @brief Returns the shared D3D12 device that backs On12, exposed to WarcraftXL.dll.
 * @return The shared device.
 */
extern "C" __declspec(dllexport) ID3D12Device* WxlD3D12Device()
{
    return wxl::gpu::Device();
}

/**
 * @brief Returns the shared render queue that backs On12, exposed to WarcraftXL.dll.
 * @return The shared command queue.
 */
extern "C" __declspec(dllexport) ID3D12CommandQueue* WxlD3D12Queue()
{
    return wxl::gpu::Queue();
}

/** @brief Flushes the D3D12 debug-layer validation messages to the proxy log. */
extern "C" __declspec(dllexport) void WxlD3D12DrainDebug()
{
    wxl::gpu::DrainDebug();
}

/**
 * @brief Loads the real d3d9 on process attach.
 * @param reason  DLL notification reason.
 * @return TRUE.
 */
BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
        EnsureReal();
    return TRUE;
}
