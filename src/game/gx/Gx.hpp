// gx bindings: the live D3D9 device and a typed facade scripts draw through.
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

#pragma once

#include <cstdint>

#include "offsets/engine/Gx.hpp"

/**
 * @brief Live D3D9 device access and a thin typed facade scripts draw through.
 *
 * RawDevice() returns the live D3D9 device. Device9 is a thin typed facade over its vtable: every
 * method is a zero-overhead inline vtable call with no facade vtable of its own, safe in any path.
 * Method names mirror IDirect3DDevice9.
 */
namespace wxl::game::gx
{
    namespace off = wxl::offsets::engine::gx;

    // Standard D3D9 enum values a script passes to the facade. These are public D3D constants (not
    // client offsets), exposed here so a script never includes the internal offsets headers.
    namespace rs    { constexpr unsigned kZEnable = 7, kZWrite = 14, kSrcBlend = 19, kDestBlend = 20,
                                         kCullMode = 22, kZFunc = 23, kAlphaBlend = 27, kStencilEnable = 52; }
    namespace blend { constexpr unsigned kSrcAlpha = 5, kInvSrcAlpha = 6; }
    namespace cmp   { constexpr unsigned kLessEqual = 4; }
    namespace cull  { constexpr unsigned kNone = 1; }
    namespace clear { constexpr unsigned kTarget = 1, kZBuffer = 2, kStencil = 4; }
    namespace prim  { constexpr int kTriangleStrip = 5; }

    /**
     * @brief Fetches a vtable slot of an object as a typed function pointer.
     * @param obj  the object whose vtable to read.
     * @param idx  the vtable slot index.
     * @return slot idx typed as Fn.
     */
    template <class Fn>
    inline Fn Vtbl(void* obj, unsigned idx)
    {
        return reinterpret_cast<Fn>((*reinterpret_cast<void***>(obj))[idx]);
    }

    /**
     * @brief Releases a COM object obtained from a Get* call.
     * @param obj  the COM object to release; ignored if null.
     */
    inline void Release(void* obj)
    {
        if (obj) Vtbl<unsigned long(__stdcall*)(void*)>(obj, 2)(obj);
    }

    /**
     * @brief Returns the live D3D9 device.
     * @return the device, or null if the graphics device is not up yet.
     */
    inline void* RawDevice()
    {
        void* g = *reinterpret_cast<void**>(off::kGxDevicePtr);
        if (!g) return nullptr;
        return *reinterpret_cast<void**>(reinterpret_cast<char*>(g) + off::kD3DDeviceField);
    }

    /**
     * @brief Thin typed facade over a D3D9 device vtable.
     *
     * Each method is a zero-overhead inline vtable call; method names mirror IDirect3DDevice9.
     */
    class Device9
    {
    public:
        /**
         * @brief Wraps a raw D3D9 device pointer.
         * @param dev  the raw device pointer, possibly null.
         */
        explicit Device9(void* dev) : dev_(dev) {}

        /** @brief Returns the wrapped raw device pointer. */
        void*    raw() const { return dev_; }

        /** @brief Returns true if a device is wrapped. */
        explicit operator bool() const { return dev_ != nullptr; }

        /**
         * @brief Sets a render state.
         * @param state  the render state index.
         * @param value  the value to set.
         * @return the device call result.
         */
        long SetRenderState(unsigned state, unsigned value)
        { return Call<long(__stdcall*)(void*, unsigned, unsigned)>(off::vt::kSetRenderState)(dev_, state, value); }

        /**
         * @brief Reads a render state.
         * @param state  the render state index.
         * @return the current value of the render state.
         */
        unsigned GetRenderState(unsigned state)
        { unsigned v = 0; Call<long(__stdcall*)(void*, unsigned, unsigned*)>(off::vt::kGetRenderState)(dev_, state, &v); return v; }

        /**
         * @brief Sets a render-target surface.
         * @param index    the render-target slot index.
         * @param surface  the surface to bind.
         * @return the device call result.
         */
        long SetRenderTarget(unsigned index, void* surface)
        { return Call<long(__stdcall*)(void*, unsigned, void*)>(off::vt::kSetRenderTarget)(dev_, index, surface); }

        /**
         * @brief Reads a render-target surface.
         * @param index       the render-target slot index.
         * @param outSurface  receives the bound surface.
         * @return the device call result.
         */
        long GetRenderTarget(unsigned index, void** outSurface)
        { return Call<long(__stdcall*)(void*, unsigned, void**)>(off::vt::kGetRenderTarget)(dev_, index, outSurface); }

        /**
         * @brief Sets the depth-stencil surface.
         * @param surface  the depth-stencil surface to bind.
         * @return the device call result.
         */
        long SetDepthStencil(void* surface)
        { return Call<long(__stdcall*)(void*, void*)>(off::vt::kSetDepthStencil)(dev_, surface); }

        /**
         * @brief Reads the depth-stencil surface.
         * @param outSurface  receives the bound depth-stencil surface.
         * @return the device call result.
         */
        long GetDepthStencil(void** outSurface)
        { return Call<long(__stdcall*)(void*, void**)>(off::vt::kGetDepthStencil)(dev_, outSurface); }

        /**
         * @brief Sets the active pixel shader.
         * @param shader  the pixel shader to bind.
         * @return the device call result.
         */
        long SetPixelShader(void* shader)
        { return Call<long(__stdcall*)(void*, void*)>(off::vt::kSetPixelShader)(dev_, shader); }

        /**
         * @brief Reads the active pixel shader.
         * @param outShader  receives the bound pixel shader.
         * @return the device call result.
         */
        long GetPixelShader(void** outShader)
        { return Call<long(__stdcall*)(void*, void**)>(off::vt::kGetPixelShader)(dev_, outShader); }

        /**
         * @brief Writes pixel-shader float constants.
         * @param startReg   the first constant register.
         * @param data       the float data to write.
         * @param vec4Count  the number of vec4 registers to write.
         * @return the device call result.
         */
        long SetPixelShaderConstantF(unsigned startReg, const float* data, unsigned vec4Count)
        { return Call<long(__stdcall*)(void*, unsigned, const float*, unsigned)>(off::vt::kSetPixelShaderConstantF)(dev_, startReg, data, vec4Count); }

        /**
         * @brief Sets the active vertex shader.
         * @param shader  the vertex shader to bind.
         * @return the device call result.
         */
        long SetVertexShader(void* shader)
        { return Call<long(__stdcall*)(void*, void*)>(off::vt::kSetVertexShader)(dev_, shader); }

        /**
         * @brief Binds a texture to a sampler stage.
         * @param stage    the sampler stage index.
         * @param texture  the texture to bind.
         * @return the device call result.
         */
        long SetTexture(unsigned stage, void* texture)
        { return Call<long(__stdcall*)(void*, unsigned, void*)>(off::vt::kSetTexture)(dev_, stage, texture); }

        /**
         * @brief Clears render-target, depth, and/or stencil buffers.
         * @param rectCount  the number of clear rects, or 0 for the whole target.
         * @param rects      the clear rects, or null.
         * @param flags      which buffers to clear.
         * @param color      the clear color.
         * @param z          the depth clear value.
         * @param stencil    the stencil clear value.
         * @return the device call result.
         */
        long Clear(unsigned rectCount, const void* rects, unsigned flags, unsigned color, float z, unsigned stencil)
        { return Call<long(__stdcall*)(void*, unsigned, const void*, unsigned, unsigned, float, unsigned)>(off::vt::kClear)(dev_, rectCount, rects, flags, color, z, stencil); }

        /**
         * @brief Draws indexed primitives from the bound streams.
         * @param primType    the primitive type.
         * @param baseVertex  the vertex index offset added to each index.
         * @param minIndex    the minimum vertex index referenced.
         * @param numVerts    the number of vertices referenced.
         * @param startIndex  the first index to read.
         * @param primCount   the number of primitives to draw.
         * @return the device call result.
         */
        long DrawIndexedPrimitive(int primType, int baseVertex, unsigned minIndex, unsigned numVerts,
                                  unsigned startIndex, unsigned primCount)
        { return Call<long(__stdcall*)(void*, int, int, unsigned, unsigned, unsigned, unsigned)>(off::vt::kDrawIndexedPrimitive)(dev_, primType, baseVertex, minIndex, numVerts, startIndex, primCount); }

        /**
         * @brief Reads the viewport into a 24-byte D3DVIEWPORT9 buffer.
         * @param viewport24  receives the 24-byte viewport.
         * @return the device call result.
         */
        long GetViewport(void* viewport24)
        { return Call<long(__stdcall*)(void*, void*)>(off::vt::kGetViewport)(dev_, viewport24); }

        /**
         * @brief Writes the viewport from a 24-byte D3DVIEWPORT9 buffer.
         * @param viewport24  the 24-byte viewport to set.
         * @return the device call result.
         */
        long SetViewport(const void* viewport24)
        { return Call<long(__stdcall*)(void*, const void*)>(off::vt::kSetViewport)(dev_, viewport24); }

    private:
        /**
         * @brief Fetches a vtable slot of the wrapped device as a typed function pointer.
         * @param idx  the vtable slot index.
         * @return slot idx typed as Fn.
         */
        template <class Fn>
        Fn Call(unsigned idx) const { return Vtbl<Fn>(dev_, idx); }
        void* dev_;
    };

    /**
     * @brief Returns the current device, wrapped.
     * @return a Device9; bool(Device()) is false until graphics is up.
     */
    inline Device9 Device() { return Device9(RawDevice()); }

    // --- toolbox helpers (implemented in the .cpp) ---

    /**
     * @brief Compiles an HLSL pixel shader into a device shader.
     * @param dev     the device to create the shader on.
     * @param hlsl    the HLSL source.
     * @param target  the shader target profile, e.g. "ps_2_0".
     * @return the device pixel shader, or null on failure.
     */
    void* CompilePixelShader(Device9 dev, const char* hlsl, const char* target);

    /** @brief A render target sized to the back buffer: texture plus its level-0 surface. */
    struct RenderTarget { void* texture = nullptr; void* surface = nullptr; int width = 0; int height = 0; };

    /**
     * @brief Creates or keeps a back-buffer-sized render target of the given format.
     * @param dev        the device to create the target on.
     * @param rt         the render target to fill or reuse.
     * @param d3dFormat  the D3D surface format.
     * @return true on success, false on failure.
     */
    bool EnsureBackbufferTarget(Device9 dev, RenderTarget& rt, uint32_t d3dFormat);

    /**
     * @brief Draws a back-buffer-sized textured quad.
     * @param dev  the device to draw on.
     *
     * Samples texture stage 0 through the bound pixel shader.
     */
    void DrawFullscreenQuad(Device9 dev);
}
