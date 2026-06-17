// Terrain tile/chunk lookup entry addresses, the tile-slot grid, and runtime chunk field offsets.
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

// Terrain tile/chunk lookups, the tile-slot grid, and runtime in-memory chunk field offsets.
namespace wraith::offsets::game::adt
{
    // --- lookups ---
    // Chunk lookup (pos) -> runtime chunk object, or null when that chunk is not parsed yet. A non-null
    // result means the chunk heightmap + collision are resident.
    constexpr uintptr_t kGetChunk = 0x007B49C0;
    // Near-tile placed-object counter (chunk, &progress, total) -> count of placed-object children still
    // loading that overlap the chunk box.
    constexpr uintptr_t kNearObjectCount = 0x007B50B0;

    // --- tile-slot grid ---
    // Tile-slot grid base: a 64x64 array of tile-area pointers (stride 4). Slot index is X-major
    // (tileX * 64 + tileY).
    constexpr uintptr_t kTileSlots   = 0x00CE48D0;
    constexpr uint32_t  kTileGridDim = 64;   // tiles per axis
    constexpr size_t    kTileSlotStride = 0x04;
    // Detailed/streaming-path selector (u32).
    constexpr uintptr_t kStreamingPathSelector = 0x00CE0494;

    // --- tile-area object fields ---
    constexpr size_t kOffTileAsyncRead = 0x70; // non-zero while the tile root read is in flight
    constexpr size_t kOffTileFileBuffer = 0x80; // non-zero once the tile file buffer is allocated

    // --- runtime chunk object fields ---
    constexpr size_t kOffChunkNodeLayerCount = 0x09; // draw-node layer count

    // --- render: multi-pass terrain layers ---
    // The native terrain draw caps a chunk at 4 texture layers: the per-layer slot array (+0x34, stride
    // 0x14) ends at the baked blend-tex handle (+0x84), and the appender writes that array with NO bound
    // check. These entries drive the multi-pass extension that renders the layers beyond the 4th.

    // Per-chunk layer-loop driver: iterates the MCNK header layer count and appends each MCLY layer to
    // the render node. this-in-ECX, no stack args.
    constexpr uintptr_t kLayerLoop = 0x007B9770;
    // Append one layer to the render node (writes slot node+0x34+n*0x14, increments node+0x09). The native
    // code does NOT bound-check n, so a 5th layer overruns into the blend-tex handle at +0x84.
    constexpr uintptr_t kAppendLayer = 0x007B9250;

    // CMapRenderChunk render node layout (0xA0 bytes, pool-allocated). The layer count is at +0x09 above.
    constexpr size_t   kOffNodeLayerSlots = 0x34; // per-layer slot array base
    constexpr size_t   kNodeLayerStride   = 0x14; // one layer slot
    constexpr uint32_t kNodeMaxLayers     = 4;    // slots before the array collides with +0x84
    constexpr size_t   kOffNodeBlendTex   = 0x84; // baked RGBA blend-tex handle (sampler bind)
    // +0x88/+0x8c are NOT spare: the engine owns them as refcounted blend-texture objects and Releases
    // them in the node teardown.
    // Any layer extension must live in a DLL-side map keyed by the node pointer, or in a grown node allocation.
    constexpr size_t   kOffNodeBlendTex2  = 0x88; // 2nd blend-tex object (engine-owned, refcounted)
    constexpr size_t   kOffNodeDrawDesc   = 0x90; // DrawIndexedPrimitive argument descriptor

    // --- client patch: 8-bit alpha (genformat 2) without the do_not_fix_alpha (0x8000) MCNK flag ---
    constexpr uintptr_t kAlphaGenformatJnzDisp = 0x007B8888; // displacement byte of the JNZ
    constexpr uint8_t   kAlphaGenformatJnzFrom = 0x30;       // -> Bad-genformat error (0x007B88B9)
    constexpr uint8_t   kAlphaGenformatJnzTo   = 0xAA;       // -> 8-bit bake (0x007B8833)

    // MCLY entry (one terrain texture layer), stride 0x10.
    constexpr size_t kMclyStride       = 0x10;
    constexpr size_t kOffMclyTextureId = 0x00; // texture index into the tile texture array
    constexpr size_t kOffMclyFlags     = 0x04; // layer flags (0x100 = has alpha map, 0x40 = animate uv ...)
    constexpr size_t kOffMclyOfsMCAL   = 0x08; // byte offset of this layer's alpha map within MCAL

    // --- terrain per-layer UV scale (Phase 1) ---
    // Post-hooked to rescale c18+i.xy by the modern per-texture scale, then re-upload c18..c(18+layerCount-1).
    // __cdecl, node = first arg.
    constexpr uintptr_t kBuildTerrainConstants = 0x007D0050;
    // c18 constant data in memory (4 floats/register); c18..c21 are the 4 per-layer tiling vec4s.
    constexpr uintptr_t kVsConstC18    = 0x00D251C0;
    constexpr uint32_t  kVsConstC18Reg = 18; // start register for the re-upload
    // Per-layer slot resolved-texture pointer: node + kOffNodeLayerSlots + i*kNodeLayerStride + this.
    constexpr size_t kOffLayerSlotTexture = 0x04;
    // CTexture file-name string (NUL-terminated) inside a resolved texture object.
    constexpr size_t kOffTextureName = 0x6C;

    // --- terrain height-blend (modern _h overlay) ---
    // Get-or-load a texture by path -> texture object (0 = fail). Resolves the asset and takes one
    // reference held for the session; resolve to a bindable handle via gx::kResolveTextureHandle.
    constexpr uintptr_t kLoadTextureByName = 0x007D9990;
    using Map_LoadTextureByNameFn = void*(__cdecl*)(const char* path);

    constexpr uint32_t kHeightSampler0Selector = 0x1E; // s9,  layer0 _h
    constexpr uint32_t kHeightSampler1Selector = 0x1F; // s10, layer1 _h
    constexpr uint32_t kHeightSampler2Selector = 0x20; // s11, layer2 _h
    constexpr uint32_t kHeightSampler3Selector = 0x21; // s12, layer3 _h
    constexpr uint32_t kHeightConstReg = 27;       // c27 = (scale0, scale1, scale2, scale3)
    constexpr uint32_t kHeightOffsetReg = 29;      // c29 = (offset0, offset1, offset2, offset3)
    constexpr uint32_t kShaderTypePixel = 4;

    // Live single-pass terrain draw fn (PS path): per visible chunk it sets the layer color samplers,
    // calls the terrain-constant build (BuildTerrainConstants, where the height bind is injected).
    // Native this-in-ECX (node), no stack args, bare RET; declared __fastcall with a dummy edx to keep node in ECX.
    constexpr uintptr_t kTerrainDrawPs = 0x007D2D70;
    using Map_TerrainDrawPsFn = void(__fastcall*)(void* node, void* edx);

    // The render device stores bound shader/sampler handles in a shadow cache. The cache base is the
    // device field at this offset; each selector's slot is base + selector*kDeviceShadowSlotStride.
    // The stock pass-global pixel shader (selector 0x4e) is read back from this slot before the height
    // swap so it can be restored after the draw.
    constexpr size_t   kOffDeviceStateCache    = 0x28F4;
    constexpr size_t   kDeviceShadowSlotStride = 0x18;

    // --- signatures ---
    // Terrain constant-block build/upload: __cdecl, node = first stack arg (two trailing args passed through).
    using Map_BuildTerrainConstantsFn = void(__cdecl*)(void* node, uint32_t a1, uint32_t a2);
    // Chunk lookup (pos on stack) -> chunk object.
    using Map_GetChunkFn = void*(__cdecl*)(float* pos);
    // Near-object counter (chunk, progressOut, total) -> count.
    using Map_NearObjectCountFn = int(__cdecl*)(void* chunk, int* progressOut, int total);
    // Layer-loop driver: native this-in-ECX, no stack args.
    using Map_LayerLoopFn = void(__fastcall*)(void* chunk, void* edx);
    // Append one layer: native this-in-ECX (render node), RET 0xC, stack order (texArr, mcly, doDedup);
    // declared __fastcall with a dummy edx so the trampoline keeps the three stack args in place.
    using Map_AppendLayerFn = void(__fastcall*)(void* node, void* edx, void* texArr, void* mcly, int doDedup);
}
