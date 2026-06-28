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

namespace
{
    using wxl::gpu::Log;

    // The engine backbuffer is X8R8G8B8 (DXGI B8G8R8X8_UNORM), which flip/composition swapchains reject, so
    // the swapchain is B8G8R8A8 and a shader blit bridges the two formats (a plain copy between the two
    // format families is not allowed).
    constexpr DXGI_FORMAT kSwapFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
    constexpr UINT        kSrvRing    = 8;
    constexpr UINT        kRtvRing    = 4;

    // Our own queue (decoupled from the On12 present queue) + copy machinery, created once.
    ID3D12CommandQueue*        g_queue = nullptr;
    ID3D12CommandAllocator*    g_alloc = nullptr;
    ID3D12GraphicsCommandList* g_list  = nullptr;
    ID3D12Fence*               g_fence = nullptr;
    UINT64                     g_fenceVal = 0;
    HANDLE                     g_event = nullptr;

    // Blit pipeline (fullscreen triangle sampling the backbuffer into the swapchain target).
    ID3D12RootSignature* g_rootSig = nullptr;
    ID3D12PipelineState* g_pso     = nullptr;
    ID3D12DescriptorHeap* g_srvHeap = nullptr;
    ID3D12DescriptorHeap* g_rtvHeap = nullptr;
    UINT g_srvInc = 0, g_rtvInc = 0, g_ring = 0;

    // Composition swapchain layered onto the engine window through DirectComposition (the engine window
    // already owns an On12 swapchain, so a composition swapchain composites over it instead).
    IDXGISwapChain3*            g_swap   = nullptr;
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
        if (g_list) return true;
        ID3D12Device* dev = wxl::gpu::Device();
        if (!dev) return false;

        D3D12_COMMAND_QUEUE_DESC qd = {};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&g_queue)))) return false;
        if (FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_alloc)))) return false;
        if (FAILED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_alloc, nullptr, IID_PPV_ARGS(&g_list)))) return false;
        g_list->Close();
        if (FAILED(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence)))) return false;
        g_event = CreateEventA(nullptr, FALSE, FALSE, nullptr);

        D3D12_DESCRIPTOR_HEAP_DESC sh = {};
        sh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        sh.NumDescriptors = kSrvRing;
        sh.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(dev->CreateDescriptorHeap(&sh, IID_PPV_ARGS(&g_srvHeap)))) return false;
        g_srvInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        D3D12_DESCRIPTOR_HEAP_DESC rh = {};
        rh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rh.NumDescriptors = kRtvRing;
        if (FAILED(dev->CreateDescriptorHeap(&rh, IID_PPV_ARGS(&g_rtvHeap)))) return false;
        g_rtvInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        if (!MakePipeline(dev)) return false;

        Log("present: machinery ready (own queue %p)", g_queue);
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
            if (g_swap)   { g_swap->Release();   g_swap = nullptr; }
            if (g_visual) { g_visual->Release(); g_visual = nullptr; }
            if (g_target) { g_target->Release(); g_target = nullptr; }
            g_w = g_h = 0;
            g_hwnd = hwnd;
        }

        if (g_swap && (g_w != w || g_h != h))
        {
            if (SUCCEEDED(g_swap->ResizeBuffers(0, w, h, kSwapFormat, 0))) { g_w = w; g_h = h; return true; }
            g_swap->Release(); g_swap = nullptr;
        }

        if (!g_dcomp && FAILED(DCompositionCreateDevice2(nullptr, IID_PPV_ARGS(&g_dcomp)))) { Log("present: DCompositionCreateDevice2 failed"); return false; }
        if (!g_target && FAILED(g_dcomp->CreateTargetForHwnd(hwnd, TRUE, &g_target))) { Log("present: CreateTargetForHwnd failed"); return false; }
        if (!g_visual && FAILED(g_dcomp->CreateVisual(&g_visual))) { Log("present: CreateVisual failed"); return false; }

        IDXGIFactory4* factory = nullptr;
        if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) return false;

        DXGI_SWAP_CHAIN_DESC1 sd = {};
        sd.Width = w; sd.Height = h; sd.Format = kSwapFormat; sd.SampleDesc.Count = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount = 2;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sd.Scaling = DXGI_SCALING_STRETCH; sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

        IDXGISwapChain1* sc1 = nullptr;
        HRESULT hr = factory->CreateSwapChainForComposition(g_queue, &sd, nullptr, &sc1);
        if (SUCCEEDED(hr)) { sc1->QueryInterface(IID_PPV_ARGS(&g_swap)); sc1->Release(); }
        factory->Release();
        if (!g_swap) { Log("present: CreateSwapChainForComposition failed hr=0x%08lX", hr); return false; }

        g_visual->SetContent(g_swap);
        g_target->SetRoot(g_visual);
        g_dcomp->Commit();

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
    void Barrier(ID3D12Resource* res, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to)
    {
        D3D12_RESOURCE_BARRIER b = {};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource   = res;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b.Transition.StateBefore = from;
        b.Transition.StateAfter  = to;
        g_list->ResourceBarrier(1, &b);
    }
}

namespace wxl::gpu::present
{
    bool Present(IDirect3DDevice9* device, HWND window)
    {
        if (!device || !window) return false;
        if (!EnsureMachinery()) return false;

        if (g_dev9 != device)
        {
            if (g_on12) { g_on12->Release(); g_on12 = nullptr; }
            if (FAILED(device->QueryInterface(__uuidof(IDirect3DDevice9On12), (void**)&g_on12)) || !g_on12)
                return false;
            g_dev9 = device;
        }

        IDirect3DSurface9* surf = nullptr;
        if (FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &surf)) || !surf) return false;

        ID3D12Resource* bb12 = nullptr;
        if (FAILED(g_on12->UnwrapUnderlyingResource(surf, g_queue, __uuidof(ID3D12Resource), (void**)&bb12)) || !bb12)
        {
            surf->Release();
            return false;
        }

        D3D12_RESOURCE_DESC d = bb12->GetDesc();
        const UINT w = (UINT)d.Width, h = d.Height;
        const DXGI_FORMAT srcFmt = d.Format;
        if (d.SampleDesc.Count > 1)   // MSAA backbuffer: the single-sample blit path does not cover it yet.
        {
            g_on12->ReturnUnderlyingResource(surf, 0, nullptr, nullptr);
            bb12->Release(); surf->Release();
            return false;
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
            g_on12->ReturnUnderlyingResource(surf, 0, nullptr, nullptr);
            bb12->Release(); surf->Release();
            return false;
        }

        if (g_fence->GetCompletedValue() < g_fenceVal)
        {
            g_fence->SetEventOnCompletion(g_fenceVal, g_event);
            WaitForSingleObject(g_event, INFINITE);
        }

        const UINT idx = g_swap->GetCurrentBackBufferIndex();
        ID3D12Resource* swapBB = nullptr;
        if (FAILED(g_swap->GetBuffer(idx, IID_PPV_ARGS(&swapBB))) || !swapBB)
        {
            g_on12->ReturnUnderlyingResource(surf, 0, nullptr, nullptr);
            bb12->Release(); surf->Release();
            return false;
        }

        const UINT slot = g_ring++ % kSrvRing;
        ID3D12Device* dev = wxl::gpu::Device();

        D3D12_CPU_DESCRIPTOR_HANDLE srvCpu = g_srvHeap->GetCPUDescriptorHandleForHeapStart();
        srvCpu.ptr += (size_t)slot * g_srvInc;
        D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
        sv.Format = srcFmt;
        sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sv.Texture2D.MipLevels = 1;
        dev->CreateShaderResourceView(bb12, &sv, srvCpu);
        D3D12_GPU_DESCRIPTOR_HANDLE srvGpu = g_srvHeap->GetGPUDescriptorHandleForHeapStart();
        srvGpu.ptr += (UINT64)slot * g_srvInc;

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += (size_t)(slot % kRtvRing) * g_rtvInc;
        D3D12_RENDER_TARGET_VIEW_DESC rv = {};
        rv.Format = kSwapFormat;
        rv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        dev->CreateRenderTargetView(swapBB, &rv, rtv);

        g_alloc->Reset();
        g_list->Reset(g_alloc, nullptr);

        Barrier(bb12,   D3D12_RESOURCE_STATE_COMMON,  D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        Barrier(swapBB, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

        ID3D12DescriptorHeap* heaps[] = { g_srvHeap };
        g_list->SetDescriptorHeaps(1, heaps);
        g_list->SetGraphicsRootSignature(g_rootSig);
        g_list->SetGraphicsRootDescriptorTable(0, srvGpu);
        // The backbuffer is always at native (window) resolution -- supersampling is resolved upstream into this
        // backbuffer by the world-render detour, never by enlarging it -- so the blit samples the full [0,1].
        g_list->SetPipelineState(g_pso);
        g_list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        D3D12_VIEWPORT vp = { 0, 0, (float)dw, (float)dh, 0, 1 };
        g_list->RSSetViewports(1, &vp);
        D3D12_RECT sc = { 0, 0, (LONG)dw, (LONG)dh };
        g_list->RSSetScissorRects(1, &sc);
        g_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g_list->DrawInstanced(3, 1, 0, 0);

        Barrier(swapBB, D3D12_RESOURCE_STATE_RENDER_TARGET,        D3D12_RESOURCE_STATE_PRESENT);
        Barrier(bb12,   D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
        g_list->Close();

        ID3D12CommandList* lists[] = { g_list };
        g_queue->ExecuteCommandLists(1, lists);
        g_fenceVal++;
        g_queue->Signal(g_fence, g_fenceVal);

        g_on12->ReturnUnderlyingResource(surf, 1, &g_fenceVal, &g_fence);
        g_swap->Present(0, 0);

        swapBB->Release();
        bb12->Release();
        surf->Release();
        return true;
    }
}
