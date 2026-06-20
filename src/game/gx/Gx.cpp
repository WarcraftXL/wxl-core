// gx toolbox helpers: pixel-shader compile, render target, fullscreen quad.
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

#include "game/gx/Gx.hpp"

#include <d3d9.h>
#include <d3dcompiler.h>
#include <cstring>

namespace wxl::game::gx
{
    /**
     * @brief Compiles an HLSL pixel shader into a device shader.
     * @param dev     the device to create the shader on.
     * @param hlsl    the HLSL source.
     * @param target  the shader target profile, e.g. "ps_2_0".
     * @return the device pixel shader, or null on failure.
     */
    void* CompilePixelShader(Device9 dev, const char* hlsl, const char* target)
    {
        if (!dev || !hlsl) return nullptr;

        ID3DBlob* code = nullptr;
        ID3DBlob* err  = nullptr;
        HRESULT hr = D3DCompile(hlsl, std::strlen(hlsl), nullptr, nullptr, nullptr, "main", target, 0, 0, &code, &err);
        if (err) err->Release();
        if (FAILED(hr) || !code) return nullptr;

        auto* d3ddev = static_cast<IDirect3DDevice9*>(dev.raw());
        IDirect3DPixelShader9* ps = nullptr;
        hr = d3ddev->CreatePixelShader(static_cast<const DWORD*>(code->GetBufferPointer()), &ps);
        code->Release();
        return SUCCEEDED(hr) ? ps : nullptr;
    }

    /**
     * @brief Creates or keeps a back-buffer-sized render target of the given format.
     * @param dev        the device to create the target on.
     * @param rt         the render target to fill or reuse.
     * @param d3dFormat  the D3D surface format.
     * @return true on success, false on failure.
     */
    bool EnsureBackbufferTarget(Device9 dev, RenderTarget& rt, uint32_t d3dFormat)
    {
        if (!dev) return false;
        if (rt.surface) return true;

        auto* d = static_cast<IDirect3DDevice9*>(dev.raw());
        IDirect3DSurface9* bb = nullptr;
        if (FAILED(d->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) || !bb) return false;
        D3DSURFACE_DESC desc;
        bb->GetDesc(&desc);
        bb->Release();

        IDirect3DTexture9* tex = nullptr;
        if (FAILED(d->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET, static_cast<D3DFORMAT>(d3dFormat), D3DPOOL_DEFAULT, &tex, nullptr)) || !tex)
            return false;

        IDirect3DSurface9* surf = nullptr;
        tex->GetSurfaceLevel(0, &surf);
        rt.texture = tex;
        rt.surface = surf;
        rt.width   = static_cast<int>(desc.Width);
        rt.height  = static_cast<int>(desc.Height);
        return true;
    }

    /**
     * @brief Draws a back-buffer-sized textured quad sampling texture stage 0.
     * @param dev  the device to draw on.
     */
    void DrawFullscreenQuad(Device9 dev)
    {
        if (!dev) return;
        auto* d = static_cast<IDirect3DDevice9*>(dev.raw());

        D3DVIEWPORT9 vp;
        d->GetViewport(&vp);
        const float w = static_cast<float>(vp.Width);
        const float h = static_cast<float>(vp.Height);

        // XYZRHW + one texcoord, half-pixel offset so texels map 1:1 to pixels.
        struct Vtx { float x, y, z, rhw, u, v; };
        const Vtx q[4] = {
            { -0.5f,    -0.5f,    0.0f, 1.0f, 0.0f, 0.0f },
            { w - 0.5f, -0.5f,    0.0f, 1.0f, 1.0f, 0.0f },
            { -0.5f,    h - 0.5f, 0.0f, 1.0f, 0.0f, 1.0f },
            { w - 0.5f, h - 0.5f, 0.0f, 1.0f, 1.0f, 1.0f },
        };
        d->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
        d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(Vtx));
    }
}
