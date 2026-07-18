// Owns the present: shows the engine's backbuffer through our own DXGI flip swapchain on the window.
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

#include "gpu/Present.hpp"

#include "gpu/Capture.hpp"
#include "gpu/Device.hpp"

#include <d3d12.h>
#include <d3d9on12.h>
#include <dxgi1_4.h>
#include <dcomp.h>
#include <d3dcompiler.h>

#include <atomic>

namespace
{
    using wxl::gpu::Log;

    std::atomic<bool> g_resetRequired{ false };

    // The engine backbuffer is X8R8G8B8 (DXGI B8G8R8X8_UNORM), which flip/composition swapchains reject, so
    // the swapchain is B8G8R8A8 and a shader blit bridges the two formats (a plain copy between the two
    // format families is not allowed).
    constexpr DXGI_FORMAT kSwapFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
    constexpr UINT        kFrameRing  = 3;
    constexpr UINT        kSwapBuffers = 2;
    constexpr UINT        kSrvRing    = 8;
    constexpr UINT        kRtvRing    = 4;

    // Our own queue (decoupled from the On12 present queue) + copy machinery, created once. Multiple command
    // allocators keep the CPU from waiting for the immediately preceding proxy blit every frame.
    ID3D12CommandQueue*        g_queue = nullptr;
    ID3D12CommandAllocator*    g_alloc[kFrameRing] = {};
    ID3D12GraphicsCommandList* g_list[kFrameRing]  = {};
    UINT64                     g_frameFence[kFrameRing] = {};
    UINT                       g_frameHead = 0;
    ID3D12Fence*               g_fence = nullptr;
    UINT64                     g_fenceVal = 0;
    HANDLE                     g_event = nullptr;

    // Blit pipeline (fullscreen triangle sampling the backbuffer into the swapchain target).
    ID3D12RootSignature* g_rootSig = nullptr;
    ID3D12PipelineState* g_pso     = nullptr;
    ID3D12DescriptorHeap* g_srvHeap = nullptr;
    ID3D12DescriptorHeap* g_rtvHeap = nullptr;
    UINT g_srvInc = 0, g_rtvInc = 0, g_descriptorRing = 0;

    // Single-sample resolve target for an MSAA D3D9 backbuffer. The custom composition swapchain itself is
    // single-sample, so multisampled engine output must be resolved before the fullscreen shader can sample it.
    ID3D12Resource* g_msaaResolve = nullptr;
    UINT g_resolveW = 0, g_resolveH = 0;
    DXGI_FORMAT g_resolveFormat = DXGI_FORMAT_UNKNOWN;

    // Composition swapchain layered onto the engine window through DirectComposition (the engine window
    // already owns an On12 swapchain, so a composition swapchain composites over it instead).
    IDXGISwapChain3*            g_swap   = nullptr;
    ID3D12Resource*             g_swapBuffer[kSwapBuffers] = {};
    IDCompositionDesktopDevice* g_dcomp  = nullptr;
    IDCompositionTarget*        g_target = nullptr;
    IDCompositionVisual2*       g_visual = nullptr;
    HWND             g_hwnd = nullptr;
    UINT             g_w = 0, g_h = 0;

    IDirect3DDevice9On12* g_on12 = nullptr;
    IDirect3DDevice9*     g_dev9 = nullptr;

    static const char* k_blitHLSL =
        "Texture2D    src : register(t0);\n"
        "SamplerState smp : register(s0);\n"
        "void vs(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD0) {\n"
        "  uv = float2((id << 1) & 2, id & 2);\n"
        "  pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);\n"
        "}\n"
        "float4 ps(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {\n"
        "  return float4(src.Sample(smp, uv).rgb, 1);\n"
        "}\n";

    /**
     * @brief Builds the blit root signature and pipeline state (fullscreen triangle into a B8G8R8A8 target).
     * @param dev  the shared D3D12 device.
     * @return true on success.
     */
    bool MakePipeline(ID3D12Device* dev)
    {
        ID3DBlob* vs = nullptr; ID3DBlob* ps = nullptr; ID3DBlob* err = nullptr;
        if (FAILED(D3DCompile(k_blitHLSL, strlen(k_blitHLSL), nullptr, nullptr, nullptr, "vs", "vs_5_0", 0, 0, &vs, &err)))
        {
            if (err) Log("present: blit VS error: %s", (const char*)err->GetBufferPointer());
            return false;
        }
        if (FAILED(D3DCompile(k_blitHLSL, strlen(k_blitHLSL), nullptr, nullptr, nullptr, "ps", "ps_5_0", 0, 0, &ps, &err)))
        {
            if (err) Log("present: blit PS error: %s", (const char*)err->GetBufferPointer());
            vs->Release();
            return false;
        }

        D3D12_DESCRIPTOR_RANGE range = {};
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = 1;
        D3D12_ROOT_PARAMETER params[1] = {};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[0].DescriptorTable.NumDescriptorRanges = 1;
        params[0].DescriptorTable.pDescriptorRanges = &range;
        D3D12_STATIC_SAMPLER_DESC samp = {};
        samp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samp.AddressU = samp.AddressV = samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        samp.MaxLOD = D3D12_FLOAT32_MAX;
        D3D12_ROOT_SIGNATURE_DESC rs = {};
        rs.NumParameters = 1; rs.pParameters = params;
        rs.NumStaticSamplers = 1; rs.pStaticSamplers = &samp;
        rs.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ID3DBlob* sig = nullptr;
        if (FAILED(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err))) { vs->Release(); ps->Release(); return false; }
        dev->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&g_rootSig));
        sig->Release();

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
        pd.pRootSignature = g_rootSig;
        pd.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
        pd.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
        pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pd.SampleMask = UINT_MAX;
        pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pd.NumRenderTargets = 1;
        pd.RTVFormats[0] = kSwapFormat;
        pd.SampleDesc.Count = 1;
        HRESULT hr = dev->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&g_pso));
        vs->Release(); ps->Release();
        if (FAILED(hr)) { Log("present: blit PSO failed hr=0x%08lX", hr); return false; }
        return true;
    }

    /**
     * @brief Creates our queue, copy machinery, descriptor heaps, and blit pipeline once.
     * @return true when ready.
     */
    bool EnsureMachinery()
    {
        if (g_list[0]) return true;
        ID3D12Device* dev = wxl::gpu::Device();
        if (!dev) return false;

        D3D12_COMMAND_QUEUE_DESC qd = {};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        HRESULT hr = dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&g_queue));
        if (FAILED(hr)) { Log("present: CreateCommandQueue failed hr=0x%08lX", (unsigned long)hr); return false; }
        for (UINT i = 0; i < kFrameRing; ++i)
        {
            hr = dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_alloc[i]));
            if (FAILED(hr)) { Log("present: CreateCommandAllocator[%u] failed hr=0x%08lX", i, (unsigned long)hr); return false; }
            hr = dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_alloc[i], nullptr, IID_PPV_ARGS(&g_list[i]));
            if (FAILED(hr)) { Log("present: CreateCommandList[%u] failed hr=0x%08lX", i, (unsigned long)hr); return false; }
            g_list[i]->Close();
        }
        hr = dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence));
        if (FAILED(hr)) { Log("present: CreateFence failed hr=0x%08lX", (unsigned long)hr); return false; }
        g_event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
        if (!g_event) { Log("present: CreateEvent failed win32=%lu", GetLastError()); return false; }

        D3D12_DESCRIPTOR_HEAP_DESC sh = {};
        sh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        sh.NumDescriptors = kSrvRing;
        sh.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        hr = dev->CreateDescriptorHeap(&sh, IID_PPV_ARGS(&g_srvHeap));
        if (FAILED(hr)) { Log("present: Create SRV heap failed hr=0x%08lX", (unsigned long)hr); return false; }
        g_srvInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        D3D12_DESCRIPTOR_HEAP_DESC rh = {};
        rh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rh.NumDescriptors = kRtvRing;
        hr = dev->CreateDescriptorHeap(&rh, IID_PPV_ARGS(&g_rtvHeap));
        if (FAILED(hr)) { Log("present: Create RTV heap failed hr=0x%08lX", (unsigned long)hr); return false; }
        g_rtvInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        if (!MakePipeline(dev)) return false;

        Log("present: machinery ready (own queue %p)", g_queue);
        return true;
    }

    void ReleaseSwapBuffers()
    {
        for (ID3D12Resource*& buffer : g_swapBuffer)
        {
            if (buffer) buffer->Release();
            buffer = nullptr;
        }
    }

    bool AcquireSwapBuffers()
    {
        ReleaseSwapBuffers();
        if (!g_swap) return false;
        ID3D12Device* dev = wxl::gpu::Device();
        if (!dev) return false;
        for (UINT i = 0; i < kSwapBuffers; ++i)
        {
            if (FAILED(g_swap->GetBuffer(i, IID_PPV_ARGS(&g_swapBuffer[i]))) || !g_swapBuffer[i])
            {
                ReleaseSwapBuffers();
                return false;
            }
            D3D12_CPU_DESCRIPTOR_HANDLE rtv = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
            rtv.ptr += static_cast<size_t>(i) * g_rtvInc;
            D3D12_RENDER_TARGET_VIEW_DESC desc = {};
            desc.Format = kSwapFormat;
            desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            dev->CreateRenderTargetView(g_swapBuffer[i], &desc, rtv);
        }
        return true;
    }

    void ReleaseMsaaResolve()
    {
        if (g_msaaResolve) { g_msaaResolve->Release(); g_msaaResolve = nullptr; }
        g_resolveW = g_resolveH = 0;
        g_resolveFormat = DXGI_FORMAT_UNKNOWN;
    }

    bool EnsureMsaaResolve(ID3D12Device* dev, UINT width, UINT height, DXGI_FORMAT format)
    {
        if (g_msaaResolve && g_resolveW == width && g_resolveH == height && g_resolveFormat == format)
            return true;
        if (g_msaaResolve)
        {
            // A dimension/format change normally follows PrepareForReset, which drains and releases this target.
            // Refuse an unsafe replacement if a driver changed it without a reset boundary.
            Log("present: MSAA resolve shape changed without reset (%ux%u/%d -> %ux%u/%d)",
                g_resolveW, g_resolveH, (int)g_resolveFormat, width, height, (int)format);
            return false;
        }

        D3D12_HEAP_PROPERTIES heap = {};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        const HRESULT hr = dev->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON,
            nullptr, IID_PPV_ARGS(&g_msaaResolve));
        if (FAILED(hr) || !g_msaaResolve)
        {
            Log("present: MSAA resolve target %ux%u fmt=%d failed hr=0x%08lX",
                width, height, (int)format, (unsigned long)hr);
            return false;
        }
        g_resolveW = width;
        g_resolveH = height;
        g_resolveFormat = format;
        Log("present: MSAA resolve target ready %ux%u fmt=%d", width, height, (int)format);
        return true;
    }

    /**
     * @brief Creates or resizes the composition swapchain and its DirectComposition host on the window.
     * @param hwnd  the engine window.
     * @param w     target width.
     * @param h     target height.
     * @return true when the swapchain is ready at the requested size.
     */
    bool EnsureSwapchain(HWND hwnd, UINT w, UINT h)
    {
        if (g_swap && g_hwnd == hwnd && g_w == w && g_h == h) return true;

        // A graphics restart recreates the engine's window; the composition target is bound to the old hwnd,
        // so tear the composition objects down and rebuild them on the new window (the dcomp device is reused).
        if (g_hwnd != hwnd)
        {
            ReleaseSwapBuffers();
            if (g_swap)   { g_swap->Release();   g_swap = nullptr; }
            if (g_visual) { g_visual->Release(); g_visual = nullptr; }
            if (g_target) { g_target->Release(); g_target = nullptr; }
            g_w = g_h = 0;
            g_hwnd = hwnd;
        }

        if (g_swap && (g_w != w || g_h != h))
        {
            ReleaseSwapBuffers();
            if (SUCCEEDED(g_swap->ResizeBuffers(0, w, h, kSwapFormat, 0)) && AcquireSwapBuffers())
            {
                g_w = w; g_h = h; return true;
            }
            g_swap->Release(); g_swap = nullptr;
        }

        HRESULT hr = S_OK;
        if (!g_dcomp && FAILED(hr = DCompositionCreateDevice2(nullptr, IID_PPV_ARGS(&g_dcomp)))) { Log("present: DCompositionCreateDevice2 failed hr=0x%08lX", (unsigned long)hr); return false; }
        if (!g_target && FAILED(hr = g_dcomp->CreateTargetForHwnd(hwnd, TRUE, &g_target))) { Log("present: CreateTargetForHwnd failed hr=0x%08lX hwnd=%p", (unsigned long)hr, hwnd); return false; }
        if (!g_visual && FAILED(hr = g_dcomp->CreateVisual(&g_visual))) { Log("present: CreateVisual failed hr=0x%08lX", (unsigned long)hr); return false; }

        IDXGIFactory4* factory = nullptr;
        hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
        if (FAILED(hr)) { Log("present: CreateDXGIFactory2 failed hr=0x%08lX", (unsigned long)hr); return false; }

        DXGI_SWAP_CHAIN_DESC1 sd = {};
        sd.Width = w; sd.Height = h; sd.Format = kSwapFormat; sd.SampleDesc.Count = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount = kSwapBuffers;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sd.Scaling = DXGI_SCALING_STRETCH; sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

        IDXGISwapChain1* sc1 = nullptr;
        hr = factory->CreateSwapChainForComposition(g_queue, &sd, nullptr, &sc1);
        if (SUCCEEDED(hr))
        {
            const HRESULT qhr = sc1->QueryInterface(IID_PPV_ARGS(&g_swap));
            if (FAILED(qhr)) Log("present: swapchain3 QueryInterface failed hr=0x%08lX", (unsigned long)qhr);
            sc1->Release();
        }
        factory->Release();
        if (!g_swap) { Log("present: CreateSwapChainForComposition failed hr=0x%08lX", hr); return false; }
        if (!AcquireSwapBuffers())
        {
            Log("present: composition backbuffer acquisition failed");
            g_swap->Release(); g_swap = nullptr;
            return false;
        }

        hr = g_visual->SetContent(g_swap);
        if (FAILED(hr)) { Log("present: SetContent failed hr=0x%08lX", (unsigned long)hr); return false; }
        hr = g_target->SetRoot(g_visual);
        if (FAILED(hr)) { Log("present: SetRoot failed hr=0x%08lX", (unsigned long)hr); return false; }
        hr = g_dcomp->Commit();
        if (FAILED(hr)) { Log("present: DComp Commit failed hr=0x%08lX", (unsigned long)hr); return false; }
        Log("present: DComp commit submitted %ux%u hwnd=%p", w, h, hwnd);
        hr = g_dcomp->WaitForCommitCompletion();
        if (FAILED(hr)) { Log("present: DComp commit wait failed hr=0x%08lX", (unsigned long)hr); return false; }

        g_w = w; g_h = h;
        Log("present: composition swapchain ready %ux%u hwnd=%p", w, h, hwnd);
        return true;
    }

    /**
     * @brief Issues a resource transition on the recording command list.
     * @param res   resource to transition.
     * @param from  current state.
     * @param to    target state.
     */
    void Barrier(ID3D12GraphicsCommandList* list, ID3D12Resource* res,
                 D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to)
    {
        D3D12_RESOURCE_BARRIER b = {};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource   = res;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b.Transition.StateBefore = from;
        b.Transition.StateAfter  = to;
        list->ResourceBarrier(1, &b);
    }

}

namespace wxl::gpu::present
{
    bool ResetRequired()
    {
        return g_resetRequired.load(std::memory_order_relaxed);
    }

    void MarkRealReset()
    {
        g_resetRequired.store(false, std::memory_order_relaxed);
    }

    bool PrepareForReset()
    {
        // A reset can arrive immediately after the last Present, before the next frame's normal fence wait.
        // D3D9On12 may otherwise wait forever while rebuilding its swapchain around a backbuffer that our
        // private queue is still sampling. Bound the wait so a removed/stuck GPU cannot freeze Wow.exe.
        if (g_fence && g_fence->GetCompletedValue() < g_fenceVal)
        {
            // 1000 ms: a healthy queue drains in single-digit ms; a GPU that needs longer is stuck,
            // and the engine's DEVICELOST retry loop would compound a 5 s cap into multi-second hangs.
            if (FAILED(g_fence->SetEventOnCompletion(g_fenceVal, g_event)) ||
                WaitForSingleObject(g_event, 1000) != WAIT_OBJECT_0)
            {
                ID3D12Device* device = wxl::gpu::Device();
                const HRESULT reason = device ? device->GetDeviceRemovedReason() : E_FAIL;
                Log("present: reset drain timed out fence=%llu/%llu removedReason=0x%08lX",
                    (unsigned long long)g_fence->GetCompletedValue(),
                    (unsigned long long)g_fenceVal, (unsigned long)reason);
                return false;
            }
        }

        ReleaseMsaaResolve();
        // Keep the composition swapchain attached. It is independent from D3D9On12 and retaining its last
        // completed frame covers the engine's reset interval; detaching it exposes On12's blank white window.
        // EnsureSwapchain resizes it lazily on the first Present if the client dimensions actually changed.
        Log("present: reset preparation complete fence=%llu", (unsigned long long)g_fenceVal);
        return true;
    }

    bool Present(IDirect3DDevice9* device, HWND window)
    {
        static UINT attempts = 0;
        static bool loggedSuccess = false;
        const bool trace = attempts++ < 8;
        if (!device || !window)
        {
            if (trace) Log("present: rejected null device=%p window=%p", device, window);
            return false;
        }
        // Minimized: nothing is visible and the composition swapchain retains its last frame, so
        // skip the whole blit/submit machinery instead of pushing frames into an occluded chain.
        // Returning true tells the capture hook the present is handled (native present stays off).
        // Latch the occlusion: the engine's post-restore Reset must then run natively (see
        // ResetRequired) even though its params look unchanged.
        if (IsIconic(window)) { g_resetRequired.store(true, std::memory_order_relaxed); return true; }
        if (trace)
        {
            RECT wr{}, cr{};
            GetWindowRect(window, &wr);
            GetClientRect(window, &cr);
            Log("present: attempt=%u dev=%p hwnd=%p visible=%d iconic=%d window=[%ld,%ld,%ld,%ld] client=%ldx%ld style=0x%08lX ex=0x%08lX",
                attempts, device, window, IsWindowVisible(window), IsIconic(window),
                wr.left, wr.top, wr.right, wr.bottom, cr.right - cr.left, cr.bottom - cr.top,
                (unsigned long)GetWindowLongPtr(window, GWL_STYLE),
                (unsigned long)GetWindowLongPtr(window, GWL_EXSTYLE));
        }
        if (!EnsureMachinery())
        {
            if (trace) Log("present: EnsureMachinery failed");
            return false;
        }

        if (g_dev9 != device)
        {
            if (g_on12) { g_on12->Release(); g_on12 = nullptr; }
            const HRESULT hr = device->QueryInterface(__uuidof(IDirect3DDevice9On12), (void**)&g_on12);
            if (FAILED(hr) || !g_on12)
            {
                if (trace) Log("present: IDirect3DDevice9On12 QI failed hr=0x%08lX", (unsigned long)hr);
                return false;
            }
            g_dev9 = device;
            if (trace) Log("present: On12 interface=%p", g_on12);
        }

        IDirect3DSurface9* surf = nullptr;
        HRESULT hr = device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &surf);
        if (FAILED(hr) || !surf)
        {
            if (trace) Log("present: GetBackBuffer failed hr=0x%08lX", (unsigned long)hr);
            return false;
        }

        ID3D12Resource* bb12 = nullptr;
        hr = g_on12->UnwrapUnderlyingResource(surf, g_queue, __uuidof(ID3D12Resource), (void**)&bb12);
        if (FAILED(hr) || !bb12)
        {
            if (trace) Log("present: UnwrapUnderlyingResource failed hr=0x%08lX surf=%p queue=%p", (unsigned long)hr, surf, g_queue);
            surf->Release();
            return false;
        }

        D3D12_RESOURCE_DESC d = bb12->GetDesc();
        const UINT w = (UINT)d.Width, h = d.Height;
        const DXGI_FORMAT srcFmt = d.Format;
        const bool msaa = d.SampleDesc.Count > 1;
        ID3D12Device* dev = wxl::gpu::Device();
        ID3D12Resource* sampleSource = bb12;
        if (msaa)
        {
            if (!EnsureMsaaResolve(dev, w, h, srcFmt))
            {
                g_on12->ReturnUnderlyingResource(surf, 0, nullptr, nullptr);
                bb12->Release(); surf->Release();
                return false;
            }
            sampleSource = g_msaaResolve;
            if (trace) Log("present: resolving MSAA samples=%u quality=%u", d.SampleDesc.Count, d.SampleDesc.Quality);
        }

        // The swapchain matches the window client size. The On12 backbuffer is pinned to that same native size
        // (supersampling is resolved upstream into it, never by enlarging it), so the fullscreen blit is a plain
        // 1:1 copy from the backbuffer to the swapchain buffer.
        RECT rc = {};
        GetClientRect(window, &rc);
        UINT dw = (UINT)(rc.right - rc.left), dh = (UINT)(rc.bottom - rc.top);
        if (dw == 0 || dh == 0) { dw = w; dh = h; }

        if (!EnsureSwapchain(window, dw, dh))
        {
            if (trace) Log("present: EnsureSwapchain failed target=%ux%u source=%ux%u fmt=%d", dw, dh, w, h, (int)srcFmt);
            g_on12->ReturnUnderlyingResource(surf, 0, nullptr, nullptr);
            bb12->Release(); surf->Release();
            return false;
        }

        const UINT frameSlot = g_frameHead++ % kFrameRing;
        const UINT64 reusableAt = g_frameFence[frameSlot];
        if (g_fence->GetCompletedValue() < reusableAt)
        {
            g_fence->SetEventOnCompletion(reusableAt, g_event);
            WaitForSingleObject(g_event, INFINITE);
        }

        const UINT idx = g_swap->GetCurrentBackBufferIndex();
        ID3D12Resource* swapBB = idx < kSwapBuffers ? g_swapBuffer[idx] : nullptr;
        if (!swapBB)
        {
            g_on12->ReturnUnderlyingResource(surf, 0, nullptr, nullptr);
            bb12->Release(); surf->Release();
            return false;
        }

        const UINT slot = g_descriptorRing++ % kSrvRing;
        ID3D12GraphicsCommandList* list = g_list[frameSlot];

        D3D12_CPU_DESCRIPTOR_HANDLE srvCpu = g_srvHeap->GetCPUDescriptorHandleForHeapStart();
        srvCpu.ptr += (size_t)slot * g_srvInc;
        D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
        sv.Format = srcFmt;
        sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sv.Texture2D.MipLevels = 1;
        dev->CreateShaderResourceView(sampleSource, &sv, srvCpu);
        D3D12_GPU_DESCRIPTOR_HANDLE srvGpu = g_srvHeap->GetGPUDescriptorHandleForHeapStart();
        srvGpu.ptr += (UINT64)slot * g_srvInc;

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += static_cast<size_t>(idx) * g_rtvInc;

        g_alloc[frameSlot]->Reset();
        list->Reset(g_alloc[frameSlot], nullptr);

        if (msaa)
        {
            Barrier(list, bb12, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
            Barrier(list, g_msaaResolve, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RESOLVE_DEST);
            list->ResolveSubresource(g_msaaResolve, 0, bb12, 0, srcFmt);
            Barrier(list, bb12, D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_COMMON);
            Barrier(list, g_msaaResolve, D3D12_RESOURCE_STATE_RESOLVE_DEST,
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }
        else
            Barrier(list, bb12, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        Barrier(list, swapBB, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

        ID3D12DescriptorHeap* heaps[] = { g_srvHeap };
        list->SetDescriptorHeaps(1, heaps);
        list->SetGraphicsRootSignature(g_rootSig);
        list->SetGraphicsRootDescriptorTable(0, srvGpu);
        // The backbuffer is always at native (window) resolution -- supersampling is resolved upstream into this
        // backbuffer by the world-render detour, never by enlarging it -- so the blit samples the full [0,1].
        list->SetPipelineState(g_pso);
        list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        D3D12_VIEWPORT vp = { 0, 0, (float)dw, (float)dh, 0, 1 };
        list->RSSetViewports(1, &vp);
        D3D12_RECT sc = { 0, 0, (LONG)dw, (LONG)dh };
        list->RSSetScissorRects(1, &sc);
        list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        list->DrawInstanced(3, 1, 0, 0);

        Barrier(list, swapBB, D3D12_RESOURCE_STATE_RENDER_TARGET,         D3D12_RESOURCE_STATE_PRESENT);
        if (msaa)
            Barrier(list, g_msaaResolve, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                    D3D12_RESOURCE_STATE_COMMON);
        else
            Barrier(list, bb12, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
        list->Close();

        ID3D12CommandList* lists[] = { list };
        g_queue->ExecuteCommandLists(1, lists);
        g_fenceVal++;
        g_queue->Signal(g_fence, g_fenceVal);
        g_frameFence[frameSlot] = g_fenceVal;

        const HRESULT returnHr = g_on12->ReturnUnderlyingResource(surf, 1, &g_fenceVal, &g_fence);
        const HRESULT presentHr = g_swap->Present(0, 0);
        if (FAILED(returnHr) || FAILED(presentHr))
        {
            // A persistently failing present (e.g. device removed) would otherwise log — and query
            // the driver — every frame; sample the first few then one in 600 like the capture miss log.
            g_resetRequired.store(true, std::memory_order_relaxed); // recovery Reset must run natively
            static UINT submitFails = 0;
            ++submitFails;
            if (trace || submitFails <= 4 || (submitFails % 600) == 0)
                Log("present: submit failed #%u returnHr=0x%08lX presentHr=0x%08lX removedReason=0x%08lX",
                    submitFails, (unsigned long)returnHr, (unsigned long)presentHr,
                    (unsigned long)dev->GetDeviceRemovedReason());
            bb12->Release();
            surf->Release();
            return false;
        }

        if (!loggedSuccess)
        {
            loggedSuccess = true;
            Log("present: first frame visible source=%ux%u target=%ux%u fmt=%d hwnd=%p",
                w, h, dw, dh, (int)srcFmt, window);
            if (!IsWindowVisible(window))
            {
                ShowWindow(window, SW_SHOWNOACTIVATE);
                Log("present: showed previously hidden game window hwnd=%p", window);
            }
        }

        bb12->Release();
        surf->Release();
        return true;
    }
}
