// D3D12 plumbing for the d3d9 proxy: one shared device + render queue backing the On12 stack.
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

#include "gpu/Device.hpp"

#include "common/Config.hpp"
#include "common/Log.hpp"

#include <cstdio>
#include <cstdarg>
#include <cstdlib>

namespace
{
    ID3D12Device*       g_device    = nullptr;
    ID3D12CommandQueue* g_queue     = nullptr;
    ID3D12InfoQueue*    g_infoQueue = nullptr;
    bool                g_firstUsed = false;

    /**
     * @brief Creates a direct-type D3D12 command queue.
     * @param dev  device that owns the queue.
     * @return The new command queue, or null on failure.
     */
    ID3D12CommandQueue* MakeQueue(ID3D12Device* dev)
    {
        D3D12_COMMAND_QUEUE_DESC desc = {};
        desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        ID3D12CommandQueue* q = nullptr;
        dev->CreateCommandQueue(&desc, IID_PPV_ARGS(&q));
        return q;
    }

    /**
     * @brief Reports whether the On12 bridge may run at all: not disabled, and d3d12.dll present.
     *
     * Every d3d12 import of this proxy is delay-loaded, so this presence check is what stands between a
     * D3D12-less machine and a delay-load failure -- with it, such a machine simply runs the native
     * pass-through path. Opt-out for everyone else: WXL_NO_ON12 env or a "WarcraftXL_on12.disable" file
     * next to the client.
     * @return True when the D3D12 machinery is usable on this machine.
     */
    bool On12Usable()
    {
        static const bool usable = [] {
            if (wxl::config::Env("WXL_NO_ON12", false))
            {
                wxl::gpu::Log("d3d9proxy: On12 disabled by WXL_NO_ON12 -> native d3d9");
                return false;
            }
            if (GetFileAttributesA("WarcraftXL_on12.disable") != INVALID_FILE_ATTRIBUTES)
            {
                wxl::gpu::Log("d3d9proxy: On12 disabled by WarcraftXL_on12.disable -> native d3d9");
                return false;
            }
            if (!LoadLibraryA("d3d12.dll"))
            {
                wxl::gpu::Log("d3d9proxy: d3d12.dll unavailable -> native d3d9");
                return false;
            }
            return true;
        }();
        return usable;
    }

    /**
     * @brief Lazily creates the shared device and queue on first use.
     * @return True once the shared device and queue exist.
     */
    bool EnsureDevice()
    {
        if (g_device) return true;
        if (!On12Usable()) return false;

#ifdef WXL_D3D12_DEBUG
        // The D3D12 debug layer validates every call. With On12 funnelling all engine D3D9 calls through
        // D3D12, leaving it on costs ~3-5x FPS. Built only with -DWXL_D3D12_DEBUG.
        ID3D12Debug* dbg = nullptr;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg)))) { dbg->EnableDebugLayer(); dbg->Release(); }
#endif

        if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&g_device)))) return false;
#ifdef WXL_D3D12_DEBUG
        g_device->QueryInterface(IID_PPV_ARGS(&g_infoQueue));   // null if the debug layer is absent
#endif
        g_queue = MakeQueue(g_device);
        return g_queue != nullptr;
    }
}

namespace wxl::gpu
{
    /**
     * @brief Returns the shared D3D12 device, created on first use.
     * @return The shared device.
     */
    ID3D12Device*       Device() { EnsureDevice(); return g_device; }

    /**
     * @brief Returns the shared render queue, created on first use.
     * @return The shared command queue.
     */
    ID3D12CommandQueue* Queue()  { EnsureDevice(); return g_queue; }

    /**
     * @brief Builds On12 args; the first call uses the shared device and queue, later calls use the shared
     *        device with a fresh queue.
     * @return Populated D3D9ON12_ARGS.
     */
    D3D9ON12_ARGS MakeOn12Args()
    {
        D3D9ON12_ARGS args = {};
        args.Enable9On12 = TRUE;
        if (EnsureDevice())
        {
            args.pD3D12Device = g_device;
            if (!g_firstUsed) { args.ppD3D12Queues[0] = g_queue; g_firstUsed = true; }
            else              { args.ppD3D12Queues[0] = MakeQueue(g_device); }   // throwaway, kept by On12
            args.NumQueues = 1;
        }
        return args;
    }

    /** @brief Drains the D3D12 debug layer's stored validation messages to the log, a no-op if absent. */
    void DrainDebug()
    {
        if (!g_infoQueue) return;
        UINT64 n = g_infoQueue->GetNumStoredMessages();
        for (UINT64 i = 0; i < n && i < 30; ++i)
        {
            SIZE_T len = 0;
            g_infoQueue->GetMessage(i, nullptr, &len);
            if (!len) continue;
            D3D12_MESSAGE* m = static_cast<D3D12_MESSAGE*>(malloc(len));
            if (m && SUCCEEDED(g_infoQueue->GetMessage(i, m, &len)))
                Log("d3d12dbg[%d]: %s", static_cast<int>(m->Severity), m->pDescription);
            free(m);
        }
        g_infoQueue->ClearStoredMessages();
    }

    /**
     * @brief Writes a formatted Info line to the proxy's diagnostic log, opening it on first use.
     *
     * Routed through the shared leveled engine (this DLL's own instance): thread-safe, timestamped,
     * creates Logs\ if missing, and honors WXL_LOG_LEVEL. Proxy lines are sparse crash diagnostics,
     * so each is flushed to disk — the DLL has no orderly close on process exit.
     * @param fmt  printf-style format string followed by its arguments.
     */
    void Log(const char* fmt, ...)
    {
        ::wxl::log::Open("Logs\\d3d9proxy.log"); // idempotent
        if (!::wxl::log::Enabled(::wxl::log::Level::Info)) return;
        va_list ap;
        va_start(ap, fmt);
        ::wxl::log::WriteV(::wxl::log::Level::Info, fmt, ap);
        va_end(ap);
        ::wxl::log::Flush();
    }
}
