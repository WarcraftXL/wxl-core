// Terrain tile/chunk lookups, the tile-slot grid, and runtime in-memory chunk field offsets.
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
#include <cstddef>

// INTERNAL to the core. Terrain tile/chunk lookups, the tile-slot grid, and runtime in-memory chunk
// field offsets. Modules never include this; they use wxl::game / wxl::events.
namespace wxl::offsets::game::adt
{
    // --- lookups ---
    // Chunk lookup (pos) -> runtime chunk object, or null when that chunk is not parsed yet. A non-null
    // result means the chunk heightmap + collision are resident.
    constexpr uintptr_t kGetChunk = 0x007B49C0;
    // CMapChunk::Build (this=CMapChunk in ECX): turns one raw MCNK into a live chunk (sub-chunk pointers,
    // bbox, texture-layer units, ref spawn). The "a terrain chunk was built" point, distinct from the
    // per-frame terrain draw.
    constexpr uintptr_t kChunkBuild = 0x007C64B0;

    // Liquid-height query site that reads a LiquidType row's flag byte WITHOUT the null guard every other
    // liquid consumer has: an unknown liquid id (no row) faults here. The bytes `F6 40 08 04 74 08`
    // (test byte[eax+8],4 ; jz +8) are repatched so the test is skipped and the jump is unconditional,
    // taking the default (no water-surface bump) path like the guarded consumers.
    constexpr uintptr_t kLiquidRowFlagTest = 0x007C846C;

    // TILE-AREA teardown (CMapArea::destructor, __thiscall via ECX=area) -- NOT a chunk destructor.
    // The historical name "ChunkDestroy" was a misnomer: this is the per-TILE object (CMapArea) whose
    // raw ADT file buffer at area+0x80 is freed here while a queued async-read completion may still
    // target it; a cancel hook retires the async object at area+0x70 before the free.
    constexpr uintptr_t kTileAreaDestroy = 0x007D6E10;
    using TileAreaDestroyFn = void(__fastcall*)(void* area);
    // Deprecated aliases (same address/field, kept so no published offset is ever deleted): the old
    // names wrongly said "chunk"; the object is the CMapArea tile. kOffChunkAsyncObj duplicates
    // kOffTileAsyncRead below -- it is the SAME +0x70 field of the SAME CMapArea object.
    constexpr uintptr_t kChunkDestroy = kTileAreaDestroy;    // deprecated: use kTileAreaDestroy
    using ChunkDestroyFn = TileAreaDestroyFn;                // deprecated: use TileAreaDestroyFn
    constexpr size_t kOffChunkAsyncObj = 0x70;               // deprecated: use kOffTileAsyncRead
    // Near-tile placed-object counter (chunk, &progress, total) -> count of placed-object children still
    // loading that overlap the chunk box.
    constexpr uintptr_t kNearObjectCount = 0x007B50B0;

    // --- tile-slot grid ---
    // Tile-slot grid base: a 64x64 array of tile-area pointers (stride 4). Slot index is
    // secondFilenameNumber * 64 + firstFilenameNumber, where the two numbers are the "%d_%d" of the
    // "<Map>_%d_%d.adt" tile name (area+0x48 = first, area+0x4C = second). NOTE the old comment said
    // "X-major (tileX*64 + tileY)": that was correct only under a swapped naming where "tileX" meant
    // the SECOND filename number. Phasing's PhaseHasTile uses the true second*64+first form.
    constexpr uintptr_t kTileSlots   = 0x00CE48D0;
    constexpr uint32_t  kTileGridDim = 64;   // tiles per axis
    constexpr size_t    kTileSlotStride = 0x04;
    // Detailed/streaming-path selector (u32).
    constexpr uintptr_t kStreamingPathSelector = 0x00CE0494;

    // --- tile-area (CMapArea) object fields ---
    constexpr size_t kOffTileAsyncRead  = 0x70; // CAsyncObject*; non-zero while a tile read is in flight
    constexpr size_t kOffTileFileBuffer = 0x80; // raw ADT byte buffer; freed by kTileAreaDestroy
    constexpr size_t kOffTileFileHandle = 0x6C; // SFile* of the open tile file (closed by async destroy)
    constexpr size_t kOffTileFileSize   = 0x84; // byte size of the +0x80 buffer
    constexpr size_t kOffTileIdxFirst   = 0x48; // first  %d of "<Map>_%d_%d.adt"
    constexpr size_t kOffTileIdxSecond  = 0x4C; // second %d of "<Map>_%d_%d.adt"

    // --- tile-area load / parse seam (used by the native split-ADT reader) ---
    // CMapArea::Load (__thiscall: ECX = area, one stack arg = tile filename): opens the tile file,
    // allocates the raw buffer (+0x80/+0x84) and queues the whole-file async read (+0x70) whose
    // main-thread completion is CMapArea::AsyncLoadCallback -> CMapArea::Create.
    constexpr uintptr_t kTileAreaLoad = 0x007D7150;
    using TileAreaLoadFn = void(__fastcall*)(void* area, void* edx, const char* filename);
    // CMapArea::Create (__thiscall via ECX, no args): the monolithic top-level parser. Reads ONLY
    // MVER + the 12 MHDR offsets of the buffer at area+0x80 and stores derived pointers/counts at
    // area+0x68..+0xB8 (MCIN/MTEX/MMDX/MMID/MWMO/MWID/MDDF/MODF/MFBO/MH2O/MTXF).
    constexpr uintptr_t kTileAreaCreate = 0x007D6EF0;
    using TileAreaCreateFn = void(__fastcall*)(void* area, void* edx);
    // Native async-read completion (__cdecl, ctx = area): Create + async destroy + zero +0x70/+0x6C.
    constexpr uintptr_t kTileAreaAsyncLoadCallback = 0x007D7020;
    // CMapChunk::ProcessIffChunks (__thiscall: ECX = chunk, one stack arg = firstBuild): the
    // SEQUENTIAL sub-chunk walk over the raw MCNK at chunk+0x10C that assigns the sub-chunk data
    // pointers at chunk+0x11C..+0x13C (3.3.5 never reads the MCNK-internal ofs* fields). Called only
    // by CMapChunk::Create. firstBuild!=0 patches MCNR/MCAL/MCLQ size fields in place once.
    constexpr uintptr_t kChunkProcessIffChunks = 0x007C3A10;
    using ChunkProcessIffChunksFn = void(__fastcall*)(void* chunk, void* edx, int firstBuild);
    // Raw tile-buffer allocator/free pair (plain SMemAlloc/SMemFree wrappers, MapMem.cpp). The free
    // takes (ptr, size) but ignores size. The tile destructor frees +0x80 through the free half.
    constexpr uintptr_t kAllocRawAreaData = 0x007BFE40;
    using AllocRawAreaDataFn = void*(__cdecl*)(uint32_t size);
    constexpr uintptr_t kFreeRawAreaData = 0x007BFE60;
    using FreeRawAreaDataFn = void(__cdecl*)(void* buffer, uint32_t size);
    // WDT MPHD flags global (first dword of the 0x20-byte MPHD copy): bit1 = MCCV vertex format,
    // bit2 = big (4096-byte, 8-bit) MCAL. Consulted live at every alpha unpack site.
    constexpr uintptr_t kMphdFlags = 0x00CF08D0;

    // --- map low-detail (WDL) seam ---
    // CMap::LoadWdl (MapLowDetail.cpp): opens "<mapPath>\<mapName>.wdl", SMemAlloc's the whole file
    // into wdlState[0], then parses MVER -> optional MWMO/MWID/MODF -> MAOF -> per-tile MARE(+MAHO).
    // Convention BYTE-VERIFIED against the 3.3.5.12340 export: true __thiscall (prologue
    // 55 8B EC 81 EC 3C 01 00 00 .. 8B F9 = this out of ECX, epilogue C2 08 00 = two stack args),
    // returns 1 on success / 0 when the .wdl does not open. Single caller: CMap__Load @ 0x007BFDD2
    // with ECX = kWdlState and args (&CMap__mapPath, &CMap__mapName). Declared __fastcall with a
    // dummy EDX so the trampoline routes wdlState into the this-register.
    constexpr uintptr_t kLoadWdl = 0x007CC310;
    using LoadWdlFn = uint32_t(__fastcall*)(int* wdlState, void* edx,
                                            const char* mapPath, const char* mapName);
    // The CMap WDL state block (the ECX of kLoadWdl), an int[0x100A] global:
    //   [0]           raw .wdl file buffer (SMemAlloc; the unload SMemFree's it)
    //   [1..3]        MWMO data / MWID data / MODF data pointers (WMO-only maps; else stale-zero)
    //   [4]           MODF entry count (MODF size >> 6)
    //   [5]           MAOF offset table = 64*64 u32 file offsets, 0 = no low-detail tile
    //   [6..0x1005]   the 64x64 CMapAreaLow* tile-slot array (kWdlSlotCount entries)
    //   [0x1006..0x1009] the low-detail map-obj-def growable-array block
    // Zeroed whole at startup by the static ctor 0x007CC2C0, and on every map unload by
    // CMap::UnloadWdl 0x007CC770 (frees+zeroes every slot, zeroes [1..5]/[0x1007], SMemFree's [0]).
    // CMap__Load runs CMap__Purge (-> 0x007CC770 @ 0x007C3843) BEFORE kLoadWdl, so the block is
    // always clean when kLoadWdl is entered.
    constexpr uintptr_t kWdlState     = 0x00CF0900;
    constexpr uint32_t  kWdlSlotCount = 64 * 64; // dimension of the [6..] slot array (0x1000)
    // CMap::AllocAreaLow: pool-allocates one CMapAreaLow (the per-tile low-detail object stored in
    // the kWdlState [6..] slots). BYTE-VERIFIED __cdecl, no args, pointer in EAX (prologue
    // 55 8B EC 83 EC 08 8B 15 18 54 D2 00 -- pool head at 0xD25418 -- plain C3 ret). Fields the
    // native kLoadWdl grid loop writes on the returned object: +0x04/+0x08/+0x0C min corner,
    // +0x10/+0x14/+0x18 max corner, +0x1C/+0x20/+0x24 center, +0x28 radius, +0x2C/+0x30 world
    // origin, +0x38/+0x3C column/row, +0x40 render-index byte budget (0xC per unholed cell),
    // +0x44 MARE heightmap data (545 s16: 17x17 outer + 16x16 inner), +0x48 MAHO hole-mask data
    // (16 u16) or 0.
    constexpr uintptr_t kAllocAreaLow = 0x007C0A90;
    using AllocAreaLowFn = void*(__cdecl*)();
    // CMap::FreeAreaLow (landmark, not called by the core): the unload 0x007CC770 releases every
    // non-null kWdlState slot through it before zeroing the slot.
    constexpr uintptr_t kFreeAreaLow = 0x007C0C60;

    // --- runtime chunk object fields ---
    constexpr size_t kOffChunkNodeLayerCount = 0x09; // draw-node (CMapRenderChunk) layer count
    // CMapChunk -> MCNK 128-byte data header (= raw MCNK ptr + 8-byte tag). The authoritative texture-layer
    // count (SMChunk.nLayers, 0..4) lives at header + 0x0C.
    constexpr size_t kOffChunkMcnkHeader = 0x110;
    constexpr size_t kOffMcnkNLayers     = 0x0C;
    // Raw on-disk MCLY/MCAL base pointers (point into the resident MCNK block, all physical entries, not
    // just the 4 materialized layers). The 4-byte field right before the MCLY payload is its sub-chunk
    // size, so physical-layer-count = *(mclyBase - 4) / 0x10.
    constexpr size_t kOffChunkMcly       = 0x12C;
    constexpr size_t kOffChunkMcal       = 0x130;
    // The full CMapChunk sub-chunk pointer block ProcessIffChunks fills (+0x11C..+0x13C). Every
    // consumer (vertex/bounds/intersect/alpha/shadow/liquid/refs/sound builds) reads these LIVE, so
    // whatever they point at must stay resident for the whole tile lifetime.
    constexpr size_t kOffChunkRawMcnk    = 0x10C; // raw MCNK (tag+size header) inside the tile buffer
    constexpr size_t kOffChunkMcvt       = 0x11C; // 145 floats, relative heights
    constexpr size_t kOffChunkMccv       = 0x120; // 145 x BGRA vertex colors (vertex format 2 only)
    constexpr size_t kOffChunkMcnr       = 0x124; // 435 signed normal bytes
    constexpr size_t kOffChunkMcsh       = 0x128; // 512-byte shadow bitmap (hdr flags bit0 gates use)
    constexpr size_t kOffChunkMcrf       = 0x134; // u32 refs: doodads first (nDoodadRefs) then wmos
    constexpr size_t kOffChunkMclq       = 0x138; // legacy liquid layers (hdr sizeLiquid > 8 gates)
    constexpr size_t kOffChunkMcse       = 0x13C; // sound emitters (hdr nSndEmitters gates)
    // Primitive/draw-batch descriptor (the 145-vertex MCVT grid VB/IB) passed to the device Draw method.
    constexpr size_t kOffChunkDrawBatch  = 0x90;
    // Source of the tile tex-owner object: (*(chunkObj+0x20) & ~1) + 8.
    constexpr size_t kOffChunkTexOwnerSrc = 0x20;
    // Per-layer record array (4 slots, stride 0x14): +0x00 flags, +0x04 diffuse CGxTex*, +0x0C alpha
    // CGxTex*, +0x10 back-ptr. Only the first nLayers (<=4) records exist.
    constexpr size_t kOffChunkLayerRecords   = 0x34;
    constexpr size_t kChunkLayerRecordStride = 0x14;

    // --- terrain surface render (per-chunk draw + multi-pass extension) ---
    // Per-chunk surface draw leaf (chunkObj is a stack arg, __cdecl): binds {layer diffuse @ sampler
    // 0x15, layer alpha @ sampler 0x16} and issues one DrawIndexedPrimitive per layer (layer 0 opaque,
    // 1..n alpha-over). The active variant is held in kSurfaceDrawFnPtr; this is the default body.
    constexpr uintptr_t kSurfaceChunkDraw  = 0x007D0D70;
    // Per-chunk draw fn-ptr the surface render dispatches through (selected by the draw-variant selector).
    constexpr uintptr_t kSurfaceDrawFnPtr  = 0x00D25098;
    // Every per-chunk surface-draw body the selector may install into kSurfaceDrawFnPtr: default, shadow,
    // and the shader/hi-detail bodies, picked by the active graphics config. These are called through the
    // indirect dispatch (__cdecl, chunkObj on the stack).
    constexpr uintptr_t kSurfaceChunkDrawVariants[] = {
        0x007D0D70, 0x007D0760, 0x007D13F0, 0x007D1AD0, 0x007D20A0, 0x007D2520,
    };
    // The shader-path per-chunk surface draw, called DIRECTLY by the surface driver (not via the indirect
    // dispatch) when the pixel-shader terrain path is active. Convention is __thiscall (chunkObj in ECX).
    // It draws one chunk per call with a single DIP: diffuse layer i at stage 0x15+i, a 4-channel combined
    // alpha RT (chunkObj+0x84) at stage 0x15+nLayers, and a Terrain1/2/3 pixel shader indexed by nLayers.
    constexpr uintptr_t kSurfaceChunkDrawShader = 0x007D2D70;
    // CGxDevice singleton; vtable + 0xA8 = the Draw (DrawIndexedPrimitive) method (batch ptr + flag).
    constexpr uintptr_t kGxDeviceSingleton = 0x00C5DF88;
    constexpr size_t    kGxDeviceDrawVtbl  = 0xA8;
    // CGxTex -> GxTex GPU handle resolve.
    constexpr uintptr_t kTexResolve        = 0x004B6CB0;
    // GxRsSet / SetTexture for a sampler slot (0x15 = diffuse stage, 0x16 = alpha stage).
    constexpr uintptr_t kSetSamplerTexture = 0x00685F50;
    // Sampler addr/filter state for the just-bound texture.
    constexpr uintptr_t kSetSamplerState   = 0x00681450;
    // Lazy texture loader for one tex-owner handle slot: slot[+4] = Load(slot[+0]).
    constexpr uintptr_t kLazyLoadTexSlot   = 0x007D6980;
    // CMapArea::LoadTextures: builds the tile tex-owner handle array (area+0x60) from the MTEX name
    // blob -- one {name, CGxTex*} slot per NUL-terminated name, indexed by MCLY.textureId, eager-
    // loading each through kLazyLoadTexSlot unless SFile streaming mode defers it. Native this-in-ECX
    // (the CMapArea) + (mtexData, mtexSize) on the stack; declared __fastcall with a dummy EDX.
    // Detoured for split tiles to source the names from the real MDID (FileDataID) instead of MTEX.
    constexpr uintptr_t kAreaLoadTextures  = 0x007D6D20;
    using Map_AreaLoadTexturesFn = void(__fastcall*)(void* area, void* edx, const void* mtexData, uint32_t mtexSize);
    // kLazyLoadTexSlot signature: __thiscall(area, slot, index); slot = { char* name; CGxTex* tex }.
    using Map_LoadTerrainTextureFn = void(__fastcall*)(void* area, void* edx, void** slot, uint32_t index);
    // Builds the per-layer alpha texture from a layer record's MCAL into record + 0x0C.
    constexpr uintptr_t kBuildLayerAlpha   = 0x007B9DE0;
    constexpr uint32_t  kSamplerDiffuse    = 0x15;
    constexpr uint32_t  kSamplerAlpha      = 0x16;
    // Tile tex-owner: per-tile texture-handle array, indexed by MCLY.textureId, stride 8
    // ([+0] = MTEX filename ptr, [+4] = loaded CGxTex*). Covers the whole tile MTEX set.
    constexpr size_t kOffTexOwnerHandleArray = 0x60;
    constexpr size_t kTexOwnerHandleStride   = 0x08;

    // --- terrain height blend ---
    // Shader-path per-chunk draw signature: native this-in-ECX, no stack args. Declared __fastcall
    // with a dummy EDX so the trampoline routes the chunk into the this-register.
    using Map_SurfaceChunkDrawShaderFn = void(__fastcall*)(void* chunkObj, void* edx);
    // By-name map texture loader: builds the wrap/filter flag set and creates the texture handle.
    // Returns the texture handle, 0 on failure. Content streams in asynchronously.
    constexpr uintptr_t kMapLoadTexture = 0x007D9990;
    using Map_LoadTextureFn = void*(__cdecl*)(const char* filename);
    // First free sampler selector on the terrain draw (s9 = selector 0x1E); s9..s15 stay engine-free
    // there (selectors 0x1E..0x24 map linearly to s9..s15). The first pass binds its four height
    // textures at s9..s12; the extra-layer second pass splits the same range: extras' heights at
    // s9..s11, the natives' combined alpha at s12, and the natives' heights at s13..s15.
    constexpr uint32_t kSamplerSelHeight0     = 0x1E;
    constexpr uint32_t kSamplerSelNativeAlpha = 0x21; // s12 in the second pass
    constexpr uint32_t kSamplerSelNativeH0    = 0x22; // s13..s15 in the second pass
    constexpr uint32_t kSamplerSelFreeCount   = 7;    // 0x1E..0x24 = s9..s15
    // First engine-free terrain pixel-shader constant: c13..c16 = per-layer (heightScale, heightOffset)
    // in the first pass. The second pass extends the range: c13..c15 = extras' pairs, c16..c19 = the
    // natives' pairs, c20 = the natives' UV-tiling ratios relative to the pass draw's layer 0.
    constexpr uint32_t kPsConstHeightBase      = 13;
    constexpr uint32_t kPsConstNativeHeight    = 16;
    constexpr uint32_t kPsConstNativeUvRatio   = 20;
    constexpr uint32_t kPsConstSecondPassCount = 8; // c13..c20 uploaded as one block
    // Served-terrain-shader contract: c21 = the extras' UV-tiling ratios; c13..c21 as one block.
    constexpr uint32_t kPsConstExtrasUvRatio   = 21;
    constexpr uint32_t kPsConstTerrainBindCount = 9;
    // Relocated served-terrain-shader block: c22..c24 extras pairs, c25..c27 native pairs, c28.y
    // native layer-3 height, c29 native uv ratios, c30 extras uv ratios (c13..c21 collided with
    // the additive shader family's own constant use; c22..c30 verified free on every permutation).
    constexpr uint32_t kPsConstTerrainBindBase = 22;
    // Signatures for kTexResolve / kSetSamplerTexture above.
    using Map_TexResolveFn  = void*(__cdecl*)(void* handle, int a, int b);
    using Map_SamplerBindFn = void(__fastcall*)(void* device, void* edx, uint32_t selector, void* tex);

    // --- terrain extra-layer second pass (layers 5..8) ---
    // Draw-node fields the second blended draw mutates and restores around the redraw. The draw leaf
    // (kSurfaceChunkDrawShader) never writes the node, so a mutate/redraw/restore inside a detour on
    // it is safe; the leaf re-runs the VS pick and full constant upload each call.
    constexpr size_t kOffChunkNodeFlags   = 0x0A; // u16: bit0 = mask-family layer, bit2 = cube-env layer
    constexpr size_t kOffChunkNodeChunk   = 0x10; // CMapChunk* backing the node
    constexpr size_t kOffChunkNodeAlphaRT = 0x84; // combined alpha texture handle bound at s(nLayers)
    // Layer record fields (record = node + kOffChunkLayerRecords + i*kChunkLayerRecordStride).
    constexpr size_t kOffLayerSlotFlags = 0x00;   // u16 MCLY flags low word (anim dir/speed/animate)
    constexpr size_t kOffLayerSlotIndex = 0x02;   // u16 slot index (0..3)
    constexpr size_t kOffLayerSlotTexId = 0x08;   // u32 MCLY textureId (dedup key only at draw time)
    constexpr size_t kOffLayerSlotAlpha = 0x0C;   // per-layer alpha handle; 0 on the shader path
    constexpr size_t kOffLayerSlotNode  = 0x10;   // back-pointer to the node
    // CMapChunk identity fields (engine-written, not data-trusted).
    constexpr size_t kOffMapChunkIndexX  = 0x24;  // local 0..15 (MCIN slot = y*16 + x)
    constexpr size_t kOffMapChunkIndexY  = 0x28;
    constexpr size_t kOffMapChunkGlobalX = 0x34;  // tileX*16 + localX (0..1023)
    constexpr size_t kOffMapChunkGlobalY = 0x38;
    // In-memory terrain texture create: builds linear/clamp flags and creates a callback-filled
    // texture handle. The fill callback is invoked by the texture system with op==1 and must write
    // *outBase / *outStride; the creation ctx arrives as its 6th argument (stride at arg 7, base at
    // arg 8, byte-verified against the native terrain fill callback).
    constexpr uintptr_t kAllocTerrainTexture = 0x007B7A70;
    using Map_AllocTerrainTextureFn = void*(__cdecl*)(uint32_t w, uint32_t h, void* ctx, void* callback,
                                                      uint32_t fmt, uint32_t fmt2);
    using Map_TexFillCallbackFn = void(__cdecl*)(int op, uint32_t w, uint32_t h, uint32_t a4,
                                                 uint32_t a5, void* ctx, uint32_t* outStride,
                                                 const void** outBase);
    constexpr uint32_t kTexFormatArgb8888 = 2;
    // Texture handle release (pairs with kAllocTerrainTexture / kMapLoadTexture).
    constexpr uintptr_t kTextureRelease = 0x0047BF30;
    using TextureReleaseFn = void(__cdecl*)(void* handle);
    // Global render-state setter (id, value): master-gated, writes the state cell + marks it dirty;
    // the next draw's state sync flushes it to the device.
    constexpr uintptr_t kGxRsSetInt = 0x00408BF0;
    using GxRsSetIntFn = void(__cdecl*)(int id, int value);
    constexpr int kGxRsBlend    = 6; // blend mode enum; 0 = opaque, 2 = srcAlpha / invSrcAlpha
    constexpr int kGxRsAlphaRef = 7; // alpha-test ref; 0 = off, 1 = discard zero-coverage pixels
    // Shadow tier getter (0 = no shadow). Tier 0 pairs the terrain PS with the unpacked-texcoord VS
    // family; tiers 1..3 pair with the packed family (layer 2/3 uvs share one texcoord register).
    constexpr uintptr_t kShadowTierGetter = 0x00873FF0;
    using ShadowTierGetterFn = int(__cdecl*)();

    // --- terrain per-layer UV scale ---
    // Builds the per-chunk terrain shader constant block (the 37 vec4s) and uploads it. c18..c21 are the
    // four per-layer UV-tiling vec4s. Post-hooked to divide each drawn layer's c18+i.xy by its texture's
    // modern scale (1<<exponent) and re-upload c18..c(18+layerCount-1). __cdecl, node = first arg; the node
    // is the CMapChunk (layer count at kOffChunkNodeLayerCount, layer slots at kOffChunkLayerRecords).
    constexpr uintptr_t kBuildTerrainConstants = 0x007D0050;
    using Map_BuildTerrainConstantsFn = void(__cdecl*)(void* node, uint32_t a1, uint32_t a2);
    // c18 constant data in memory (4 floats per register); c18 is the first per-layer tiling vec4.
    constexpr uintptr_t kVsConstC18    = 0x00D251C0;
    constexpr uint32_t  kVsConstC18Reg = 18; // start register for the re-upload
    // Resolved-texture pointer inside a layer slot (slot = node + kOffChunkLayerRecords + i*kChunkLayerRecordStride).
    constexpr size_t kOffLayerSlotTexture = 0x04;
    // File-name string (NUL-terminated) inside a resolved texture object.
    constexpr size_t kOffTextureName = 0x6C;

    // --- signatures ---
    // Chunk lookup (pos on stack) -> chunk object.
    using Map_GetChunkFn = void*(__cdecl*)(float* pos);
    // Near-object counter (chunk, progressOut, total) -> count.
    using Map_NearObjectCountFn = int(__cdecl*)(void* chunk, int* progressOut, int total);
    // CMapChunk::Build: native this-in-ECX (__thiscall, ret 8). Declared __fastcall with a dummy EDX so the
    // trampoline routes the chunk into the this-register and keeps the two stack args.
    using Map_ChunkBuildFn = void(__fastcall*)(void* chunk, void* edx, void* rawMcnk, int param2);

    // --- typed views over the objects above ---
    // The constants are the curated landmarks; these structs give named, typed access to the same fields,
    // with every member offset checked against a constant at compile time (a wrong padding fails the build).
    // Only RE'd fields are named; the gaps are explicit padding. Pointers are 4 bytes on the 32-bit client.
#pragma pack(push, 1)
    /** @brief Tile-area object (one per resident map tile): async-read state and file buffer slots. */
    struct TileArea
    {
        uint8_t  _pad00[kOffTileAsyncRead];
        uint32_t asyncRead;        // kOffTileAsyncRead (non-zero while the root read is in flight)
        uint8_t  _pad74[kOffTileFileBuffer - (kOffTileAsyncRead + sizeof(uint32_t))];
        void*    fileBuffer;       // kOffTileFileBuffer (non-zero once the file buffer is allocated)
    };
    static_assert(offsetof(TileArea, asyncRead)  == kOffTileAsyncRead,  "TileArea.asyncRead");
    static_assert(offsetof(TileArea, fileBuffer) == kOffTileFileBuffer, "TileArea.fileBuffer");

    /**
     * @brief Runtime chunk object (CMapChunk): the MCNK data-header pointer.
     *
     * The old single struct conflated two objects: nodeLayerCount @0x09 is a CMapRenderChunk (draw
     * node) field, while mcnkHeader @0x110 is a CMapChunk field. They are now two typed views --
     * MapChunk for the CMapChunk, RenderNode for the CMapRenderChunk reached via chunk+0xA8.
     */
    struct MapChunk
    {
        uint8_t  _pad00[kOffChunkMcnkHeader];
        void*    mcnkHeader;       // kOffChunkMcnkHeader -> McnkHeader (raw MCNK ptr + 8-byte tag)
    };
    static_assert(offsetof(MapChunk, mcnkHeader) == kOffChunkMcnkHeader, "MapChunk.mcnkHeader");

    /** @brief Draw node (CMapRenderChunk, chunk+0xA8): the per-node layer count. */
    struct RenderNode
    {
        uint8_t  _pad00[kOffChunkNodeLayerCount];
        uint8_t  nodeLayerCount;   // kOffChunkNodeLayerCount (draw-node layer count)
    };
    static_assert(offsetof(RenderNode, nodeLayerCount) == kOffChunkNodeLayerCount, "RenderNode.nodeLayerCount");

    /** @brief MCNK 128-byte data header (chunk->mcnkHeader): the authoritative texture-layer count. */
    struct McnkHeader
    {
        uint8_t  _pad00[kOffMcnkNLayers];
        uint32_t nLayers;          // kOffMcnkNLayers (SMChunk.nLayers, 0..4)
    };
    static_assert(offsetof(McnkHeader, nLayers) == kOffMcnkNLayers, "McnkHeader.nLayers");
#pragma pack(pop)
}
