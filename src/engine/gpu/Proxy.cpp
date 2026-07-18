// The d3d9.dll proxy: pass Direct3DCreate9(Ex) through to the system d3d9 and load WarcraftXL.dll.
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

// The client LoadLibrary's "d3d9.dll" from its own folder first, so this proxy loads ahead of the system
// one. It does exactly two things: forward the factory-create exports to the real system d3d9, and load
// WarcraftXL.dll into the process. All rendering runs on the client's native D3D9 device.

#include <windows.h>
#include <d3d9.h>

#include "common/Log.hpp"

#include <cstdarg>

namespace
{
    using Create9Fn   = IDirect3D9* (WINAPI*)(UINT);
    using Create9ExFn = HRESULT     (WINAPI*)(UINT, IDirect3D9Ex**);
    Create9Fn   g_realCreate9   = nullptr;
    Create9ExFn g_realCreate9Ex = nullptr;

    /**
     * @brief Writes one Info line to the proxy's own log sink, opening it on first use.
     *
     * The proxy is a distinct module from WarcraftXL.dll and owns a separate log instance. Lines are
     * sparse boot/crash diagnostics and are flushed immediately — the DLL has no orderly close on exit.
     * @param fmt  printf-style format string followed by its arguments.
     */
    void Log(const char* fmt, ...)
    {
        ::wxl::log::Open("Logs\\d3d9proxy.log");   // idempotent
        if (!::wxl::log::Enabled(::wxl::log::Level::Info)) return;
        va_list ap;
        va_start(ap, fmt);
        ::wxl::log::WriteV(::wxl::log::Level::Info, fmt, ap);
        va_end(ap);
        ::wxl::log::Flush();
    }

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
        if (g_realCreate9 || g_realCreate9Ex) return;
        HMODULE r = LoadRealD3D9();
        if (!r) { Log("d3d9proxy: FAILED to load the system d3d9.dll"); return; }
        g_realCreate9   = reinterpret_cast<Create9Fn>(GetProcAddress(r, "Direct3DCreate9"));
        g_realCreate9Ex = reinterpret_cast<Create9ExFn>(GetProcAddress(r, "Direct3DCreate9Ex"));
        Log("d3d9proxy: system d3d9 loaded (9=%p Ex=%p)", g_realCreate9, g_realCreate9Ex);
    }

    /**
     * @brief Loads WarcraftXL.dll once, from the first Direct3DCreate9* call.
     *
     * Deliberately NOT done in DllMain: calling LoadLibrary under the loader lock can deadlock against
     * WarcraftXL.dll's own attach work. The engine creates its factory long before the runtime is needed,
     * so first-create is early enough and runs outside the loader lock.
     */
    void EnsureRuntimeLoaded()
    {
        static bool attempted = false;
        if (attempted) return;
        attempted = true;
        if (!LoadLibraryA("WarcraftXL.dll"))
            Log("d3d9proxy: WarcraftXL.dll not loaded (win32=%lu)", GetLastError());
    }
}

/**
 * @brief Proxy entry point for Direct3DCreate9: loads the runtime, then forwards to the system d3d9.
 * @param sdkVersion  D3D SDK version passed by the caller.
 * @return The native IDirect3D9 factory, or null on failure.
 */
extern "C" IDirect3D9* WINAPI Direct3DCreate9(UINT sdkVersion)
{
    EnsureReal();
    EnsureRuntimeLoaded();
    return g_realCreate9 ? g_realCreate9(sdkVersion) : nullptr;
}

/**
 * @brief Proxy entry point for Direct3DCreate9Ex: loads the runtime, then forwards to the system d3d9.
 * @param sdkVersion  D3D SDK version passed by the caller.
 * @param out         receives the native IDirect3D9Ex factory.
 * @return S_OK on success, E_NOINTERFACE when the system d3d9 has no Ex export.
 */
extern "C" HRESULT WINAPI Direct3DCreate9Ex(UINT sdkVersion, IDirect3D9Ex** out)
{
    EnsureReal();
    EnsureRuntimeLoaded();
    if (g_realCreate9Ex) return g_realCreate9Ex(sdkVersion, out);
    if (out) *out = nullptr;
    return E_NOINTERFACE;
}

/**
 * @brief Process-attach entry point. Intentionally does no work.
 *
 * Loading the real d3d9 and WarcraftXL.dll happens lazily in Direct3DCreate9(Ex): a LoadLibrary issued
 * here would run under the loader lock and can deadlock against the loaded DLL's attach.
 * @param reason  DLL notification reason.
 * @return TRUE.
 */
BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID)
{
    (void)reason;
    return TRUE;
}
