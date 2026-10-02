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

#include "wxl/game/Binding.hpp"
#include "wxl/offsets/engine/Gx.hpp"

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
    namespace rs    { constexpr unsigned kZEnable = 7, kShadeMode = 9, kZWrite = 14, kAlphaTest = 15,
                                         kSrcBlend = 19, kDestBlend = 20, kCullMode = 22, kZFunc = 23,
                                         kAlphaBlend = 27, kFogEnable = 28, kStencilEnable = 52,
                                         kLighting = 137, kColorWrite = 168, kScissorTest = 174; }
    /// Channel mask for kColorWrite.
    namespace colorwrite { constexpr unsigned kAll = 0x0F; }
    namespace shade { constexpr unsigned kGouraud = 2; }

    // Fixed-function stage setup. A shader-driven engine leaves these wherever the last pass that used
    // them left them, so untextured geometry has to state that its colour is the vertex colour or it
    // takes whatever texture happens to still be bound.
    namespace tss  { constexpr unsigned kColorOp = 1, kColorArg1 = 2, kAlphaOp = 4, kAlphaArg1 = 5; }
    namespace top  { constexpr unsigned kDisable = 1, kSelectArg1 = 2; }
    namespace ta   { constexpr unsigned kDiffuse = 0; }

    /// Transform slots addressed by SetTransform.
    namespace ts   { constexpr unsigned kView = 2, kProjection = 3, kWorld = 256; }

    /// Vertex format flags. Position plus a per-vertex colour is all untextured line work needs.
    namespace fvf  { constexpr unsigned kXyz = 0x002, kDiffuse = 0x040; }

    namespace samp    { constexpr unsigned kAddressU = 1, kAddressV = 2, kMagFilter = 5,
                                           kMinFilter = 6, kMipFilter = 7; }
    namespace filter  { constexpr unsigned kNone = 0, kPoint = 1, kLinear = 2; }
    namespace address { constexpr unsigned kClamp = 3; }
    namespace blend { constexpr unsigned kSrcAlpha = 5, kInvSrcAlpha = 6; }
    namespace cmp   { constexpr unsigned kLessEqual = 4; }
    namespace cull  { constexpr unsigned kNone = 1; }
    namespace clear { constexpr unsigned kTarget = 1, kZBuffer = 2, kStencil = 4; }
    namespace prim  { constexpr int kLineList = 2, kTriangleList = 4, kTriangleStrip = 5; }

    /**
     * @brief Releases a COM object obtained from a Get* call.
     * @param obj  the COM object to release; ignored if null.
     */
    inline void Release(void* obj)
    {
        if (obj) Virtual<unsigned long(__stdcall*)(void*)>(obj, 2)(obj);
    }

    /**
     * @brief Returns the live D3D9 device.
     * @return the device, or null if the graphics device is not up yet.
     */
    inline void* RawDevice()
    {
        // kGxDevicePtr is a fixed-address global slot; the deref reads the graphics-device object.
        void* g = Read<void*>(off::kGxDevicePtr);
        if (!g) return nullptr;
        return static_cast<off::GxDevice*>(g)->d3dDevice;
    }

    /**
     * @brief Returns the engine's graphics-device object, which owns the D3D9 device.
     * @return the object, or null if graphics is not up yet.
     */
    inline void* RawGraphicsDevice()
    { return Read<void*>(off::kGxDevicePtr); }

    /**
     * @brief Holds the engine's projection and view across a render that overwrites them.
     *
     * The world render sets both and leaves them set. In-world that is invisible, because the client's
     * own wrapper around it puts them back and refreshes the shader system from them; calling the
     * render outside that wrapper leaves whatever draws next -- an interface, most visibly -- reading a
     * projection meant for a world scene.
     */
    class ScopedXform
    {
    public:
        ScopedXform() : device_(RawGraphicsDevice())
        {
            if (!device_) return;
            const uintptr_t base = uintptr_t(device_);
            const int slot = At<int>(base, off::kDeviceViewIndex);

            const float* projection = reinterpret_cast<const float*>(base + off::kDeviceProjection);
            const float* view       = reinterpret_cast<const float*>(base + off::kDeviceViewBase
                                                                     + size_t(slot) * off::kDeviceViewStride);
            for (int i = 0; i < 16; ++i)
            {
                savedProjection_[i] = projection[i];
                savedView_[i]       = view[i];
            }
        }

        ~ScopedXform()
        {
            if (!device_) return;
            Virtual<off::GxSetProjectionFn>(device_, off::kGxSetProjectionSlot)(device_, nullptr, savedProjection_);
            Virtual<off::GxSetProjectionFn>(device_, off::kGxSetViewSlot)(device_, nullptr, savedView_);
            Native<off::ShaderUpdateProjMatrixFn>(off::kShaderUpdateProjMatrix)();
        }

        ScopedXform(const ScopedXform&) = delete;
        ScopedXform& operator=(const ScopedXform&) = delete;

    private:
        void* device_;
        float savedProjection_[16]{};
        float savedView_[16]{};
    };

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

        /// Reading a constant back is what lets a pass restore the few registers it overwrites
        /// instead of capturing a whole state block to get them.
        long GetPixelShaderConstantF(unsigned startReg, float* data, unsigned vec4Count)
        { return Call<long(__stdcall*)(void*, unsigned, float*, unsigned)>(off::vt::kGetPixelShaderConstantF)(dev_, startReg, data, vec4Count); }

        /// Filtering and addressing are inherited from whatever drew last, so a pass that samples a
        /// target of its own has to state them rather than assume them.
        long SetSamplerState(unsigned sampler, unsigned type, unsigned value)
        { return Call<long(__stdcall*)(void*, unsigned, unsigned, unsigned)>(off::vt::kSetSamplerState)(dev_, sampler, type, value); }

        /**
         * @brief Sets the active vertex shader.
         * @param shader  the vertex shader to bind.
         * @return the device call result.
         */
        long SetVertexShader(void* shader)
        { return Call<long(__stdcall*)(void*, void*)>(off::vt::kSetVertexShader)(dev_, shader); }

        /**
         * @brief Reads the active vertex shader.
         * @param outShader  receives the bound vertex shader.
         * @return the device call result.
         */
        long GetVertexShader(void** outShader)
        { return Call<long(__stdcall*)(void*, void**)>(off::vt::kGetVertexShader)(dev_, outShader); }

        /**
         * @brief Reads the texture bound to a sampler stage.
         * @param stage       the sampler stage index.
         * @param outTexture  receives the bound texture.
         * @return the device call result.
         */
        long GetTexture(unsigned stage, void** outTexture)
        { return Call<long(__stdcall*)(void*, unsigned, void**)>(off::vt::kGetTexture)(dev_, stage, outTexture); }

        /**
         * @brief Reads a fixed-function texture stage state.
         * @param stage  the texture stage index.
         * @param type   the stage state index.
         * @return the current value of the stage state.
         */
        unsigned GetTextureStageState(unsigned stage, unsigned type)
        { unsigned v = 0; Call<long(__stdcall*)(void*, unsigned, unsigned, unsigned*)>(off::vt::kGetTextureStageState)(dev_, stage, type, &v); return v; }

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
         * @brief Draws primitives straight from caller memory, with no vertex buffer.
         * @param primType     the primitive type.
         * @param primCount    the number of primitives to draw.
         * @param vertices     the vertex data, in the format last given to SetFVF.
         * @param vertexStride the byte distance between consecutive vertices.
         * @return the device call result.
         *
         * The driver copies the data before returning, so the caller's buffer is free immediately
         * afterwards. That is what makes it the right shape for geometry rebuilt every frame.
         */
        long DrawPrimitiveUP(int primType, unsigned primCount, const void* vertices, unsigned vertexStride)
        { return Call<long(__stdcall*)(void*, int, unsigned, const void*, unsigned)>(off::vt::kDrawPrimitiveUP)(dev_, primType, primCount, vertices, vertexStride); }

        /**
         * @brief Declares the layout of the vertices passed to the fixed-function pipeline.
         * @param fvf  the flexible vertex format flags.
         * @return the device call result.
         */
        long SetFVF(unsigned fvf)
        { return Call<long(__stdcall*)(void*, unsigned)>(off::vt::kSetFVF)(dev_, fvf); }

        /**
         * @brief Writes a fixed-function transform matrix.
         * @param state   the transform slot.
         * @param matrix  sixteen floats, row-major.
         * @return the device call result.
         */
        long SetTransform(unsigned state, const float* matrix)
        { return Call<long(__stdcall*)(void*, unsigned, const float*)>(off::vt::kSetTransform)(dev_, state, matrix); }

        /**
         * @brief Reads a fixed-function transform matrix.
         * @param state    the transform slot.
         * @param matrix   receives sixteen floats, row-major.
         * @return the device call result.
         */
        long GetTransform(unsigned state, float* matrix)
        { return Call<long(__stdcall*)(void*, unsigned, float*)>(off::vt::kGetTransform)(dev_, state, matrix); }

        /**
         * @brief Sets a fixed-function texture stage state.
         * @param stage  the texture stage index.
         * @param type   the stage state index.
         * @param value  the value to set.
         * @return the device call result.
         */
        long SetTextureStageState(unsigned stage, unsigned type, unsigned value)
        { return Call<long(__stdcall*)(void*, unsigned, unsigned, unsigned)>(off::vt::kSetTextureStageState)(dev_, stage, type, value); }

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
        Fn Call(unsigned idx) const { return Virtual<Fn>(dev_, idx); }
        void* dev_;
    };

    /**
     * @brief Returns the current device, wrapped.
     * @return a Device9; bool(Device()) is false until graphics is up.
     */
    inline Device9 Device() { return Device9(RawDevice()); }

    // --- the engine's frame clear, as a detour target ---
    // Anything that draws into the frame ahead of the engine has to reckon with the engine clearing it
    // afterwards. Detouring here and dropping a bit from the flags narrows a clear rather than removing
    // it, which is what lets a background survive a pass that still needs its depth buffer wiped.

    /// Entry every scene clear goes through.
    constexpr uintptr_t kSceneClearSeam = off::kGxSceneClear;

    /// Its signature, for a detour and the matching trampoline.
    using SceneClearFn = off::GxSceneClearFn;

    /// Flag bits carried by a scene clear's first argument.
    namespace clear
    {
        constexpr uint32_t kColor = off::kSceneClearColor;
        constexpr uint32_t kDepth = off::kSceneClearDepth;
    }

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
     * @param divisor    shrinks both dimensions; 2 halves them. A pass whose output is blurred or
     *                   otherwise low-frequency costs a quarter as much at 2 and loses nothing.
     * @return true on success, false on failure.
     */
    bool EnsureBackbufferTarget(Device9 dev, RenderTarget& rt, uint32_t d3dFormat, unsigned divisor = 1);

    /**
     * @brief Releases a render target's surface + texture and zeroes it (so the next EnsureBackbufferTarget
     *        recreates it). Used on a device reset, where every D3DPOOL_DEFAULT resource must be freed.
     * @param rt  the render target to release.
     */
    void Release(RenderTarget& rt);

    /**
     * @brief Releases every render target that passed through EnsureBackbufferTarget. Called before a device
     *        reset (window resize) so the engine's Reset does not fail on a still-live D3DPOOL_DEFAULT resource.
     */
    void ReleaseResetResources();

    /**
     * @brief Draws a back-buffer-sized textured quad.
     * @param dev  the device to draw on.
     *
     * Samples texture stage 0 through the bound pixel shader.
     */
    void DrawFullscreenQuad(Device9 dev);
}
