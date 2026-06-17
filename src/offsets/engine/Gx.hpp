// Graphics-device pointer, device-cache field offsets, and render-vtable slots.
// Copyright (C) 2026 WraithEngine
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
#include <cstddef>

// Graphics-device addresses, device-object field offsets, and render-API vtable slots.
namespace wraith::offsets::engine::gx
{
    // Pointer to the engine graphics-device object. The render-API device pointer lives at
    // (device + kOffRenderDevice).
    constexpr uintptr_t kDevicePtr        = 0x00C5DF88;
    constexpr size_t    kOffRenderDevice  = 0x397C; // render-API device pointer field

    // Cached render-target surfaces on the graphics-device object.
    constexpr size_t    kOffBackBuffer    = 0x3B3C; // cached back-buffer surface
    constexpr size_t    kOffDepthSurface  = 0x3B40; // cached world depth surface

    // Render-device vtable slots (standard layout). DrawIndexedPrimitive is the single primitive-draw
    // route hooked by the draw path; the shader slots create / set / release shader objects, and the
    // constant slots upload shader constant registers.
    constexpr int kVtRelease              = 2;   // shader/COM object release
    constexpr int kVtDrawIndexedPrimitive = 82;  // (type, baseVtx, minVtx, numVtx, startIdx, primCount)
    constexpr int kVtCreateVertexShader   = 91;  // (const dword* code, out vertexShader)
    constexpr int kVtSetVertexShader      = 92;  // (vertexShader)
    constexpr int kVtSetVertexShaderConst = 94;  // (startReg, const float*, vec4Count)
    constexpr int kVtCreatePixelShader    = 106; // (const dword* code, out pixelShader)
    constexpr int kVtSetPixelShader       = 107; // (pixelShader)
    constexpr int kVtSetPixelShaderConst  = 109; // (startReg, const float*, vec4Count)

    // Engine-internal shader-constant upload (the device's own constant path), addressed as a vtable
    // byte-offset on the graphics-device object. shaderType 0 = vertex, 4 = pixel.
    constexpr int kVtSetShaderConstant    = 0x118 / 4; // byte 0x118

    // Engine-internal shader-array create: loads "<dir>\<profile>\<name>.bls" (magic GXSH, version
    // 0x10003) and fills slotsOut with permCount shader-object pointers. dir = "Shaders\\Vertex" or
    // "Shaders\\Pixel"; shaderType 0 = vertex, 4 = pixel. Native this-in-ECX(device).
    constexpr int kVtCreateShaderArray    = 0x110 / 4; // byte 0x110
    using Gx_CreateShaderArrayFn = void(__fastcall*)(void* device, void* edx, void** slotsOut, uint32_t shaderType,
                                                     const char* dir, const char* name, uint32_t permCount);

    // Engine-internal shader-constant uploader: native this-in-ECX; declared with a dummy second
    // parameter so the trampoline keeps the trailing arguments on the stack.
    using Gx_SetShaderConstantFn = void(__fastcall*)(void* device, void* edx, uint32_t shaderType, uint32_t startReg,
                                                     const float* data, uint32_t vec4Count);

    // --- multi-pass composition levers (blend mode + sampler texture) ---
    // The device combines a pass with the accumulated frame buffer using one of these blend modes.
    constexpr uint32_t kBlendOpaque    = 0; // replace
    constexpr uint32_t kBlendAdd       = 3; // additive (emissive / shine layers)
    constexpr uint32_t kBlendModulate  = 4; // multiply (lightmap / detail layers)

    // The device holds a render-state cache; the blend mode is one cached state. Set the mode by writing
    // it into the cache then marking the state dirty so the next draw re-derives the blend factors.
    constexpr size_t    kOffStateCache     = 0x28F4; // device field -> render-state cache base
    constexpr size_t    kOffCacheBlendMode = 0x90;   // blend-mode value within the cache
    constexpr int       kStateBlendIndex   = 6;      // blend-mode state index, for the dirty mark
    constexpr uintptr_t kMarkStateDirty    = 0x00685970;
    using Gx_MarkStateDirtyFn = void(__cdecl*)(int stateIndex);

    // Bind a texture object to a sampler stage. Stage 0 is sampler id 0x15. Native this-in-ECX(device);
    // declared with a dummy second parameter so the trampoline keeps the trailing arguments on the stack.
    constexpr uintptr_t kSetSamplerTexture = 0x00685F50;
    constexpr uint32_t  kSamplerStage0     = 0x15;
    using Gx_SetSamplerTextureFn = void(__fastcall*)(void* device, void* edx, uint32_t samplerId, void* texHandle);

    // The selector also routes shader-object binds: 0x4d = set vertex shader, 0x4e = set pixel shader
    // (the handle is the shader object, not a texture). Same dispatcher, same convention.
    constexpr uint32_t kSelectorSetPixelShader = 0x4E;

    // Set a texture object's wrap/clamp addressing. __cdecl(texObject, wrapS, wrapT); 1 = wrap.
    constexpr uintptr_t kSetTextureWrap = 0x00681450;
    using Gx_SetTextureWrapFn = void(__cdecl*)(void* texObject, int wrapS, int wrapT);

    // Resolve a bindable texture handle from a texture object. __cdecl(texObject, mode, callback); the WMO
    // render path calls it as (tex, 0, 0). NOT this-in-ECX, so it must be __cdecl (all args on the stack).
    constexpr uintptr_t kResolveTextureHandle = 0x004B6CB0;
    using Gx_ResolveTextureHandleFn = void*(__cdecl*)(void* texObject, int mode, void* callback);

    // --- overlay-pass depth + primitive submit (multi-pass composition) ---
    // Generic render-state setter: writes cache[stateIndex] = value and dirties it if changed, honoring the
    // device-ready guard. __cdecl(stateIndex, value).
    constexpr uintptr_t kSetDeviceState = 0x00408BF0;
    using Gx_SetDeviceStateFn = void(__cdecl*)(int stateIndex, int value);
    constexpr int    kStateZWrite     = 0x11;   // depth-write enable: 1 = on, 0 = off (overlay passes use 0)
    constexpr size_t kOffDeviceReady  = 0xF58;  // device field; nonzero = safe to touch render state

    // Primitive submit: device vtable byte offset 0xA8, __thiscall(device, const desc*, int count). desc is
    // 0x10 bytes: {u32 primType=3, u32 startIndex, u32 indexCount, u16 minIndex, u16 maxIndex}. VB/IB/stream
    // are reused from the device's already-bound state, so a replay only refills the index range.
    constexpr size_t kVtSubmitByte = 0xA8;
    using Gx_SubmitFn = int(__thiscall*)(void* device, const void* desc, int count);
}
