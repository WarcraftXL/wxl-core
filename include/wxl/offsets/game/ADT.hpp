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
    // Chunk build (this = the chunk object, in ECX): turns one raw MCNK into a live chunk (sub-chunk
    // pointers, bbox, texture-layer units, ref spawn). The "a terrain chunk was built" point, distinct
    // from the per-frame terrain draw.
    constexpr uintptr_t kChunkBuild = 0x007C64B0;

    // Liquid-height query site that reads a LiquidType row's flag byte WITHOUT the null guard every other
    // liquid consumer has: an unknown liquid id (no row) faults here. The bytes `F6 40 08 04 74 08`
    // (test byte[eax+8],4 ; jz +8) are repatched so the test is skipped and the jump is unconditional,
    // taking the default (no water-surface bump) path like the guarded consumers.
    constexpr uintptr_t kLiquidRowFlagTest = 0x007C846C;

    // LiquidType.dbc id-index globals (filled at boot by the DBC load; live for the process).
    // row = rows[id - minId], and a gap id yields a NULL row -- presence must be tested before handing
    // an id to any stock liquid consumer. minId/maxId are the inclusive id bounds of the loaded table.
    constexpr uintptr_t kLiquidTypeDbRows  = 0x00AD4084; // LiquidTypeRec** (indexed by id - minId)
    constexpr uintptr_t kLiquidTypeDbMinId = 0x00AD4074; // u32 inclusive lower bound
    constexpr uintptr_t kLiquidTypeDbMaxId = 0x00AD4070; // u32 inclusive upper bound

    // LiquidTypeRec.MaterialID: the liquid material lookup (0x008A1FA0) switches on this
    // column with no default case (1 water, 2 ooze, 3 magma) -- any other value caches a NULL
    // IMaterial* for the row, and the next liquid instance built from it null-derefs its material
    // pointer at draw time. The classic on-disk MCLQ path never reaches this with a bad id (it
    // hardcodes LiquidType id = layer-bit-index + 1, always 1..4, per the legacy liquid builder
    // 0x007C5690); the MH2O instance path (the same builder's second branch, fed by
    // FixupMh2o's normalized bytes) copies its `type` field straight through, so a row this column
    // doesn't cover is real: existence in the table is not the same as being usable.
    constexpr size_t kLiquidTypeMaterialId = 0x38; // u32, GetMaterial's usable range is {1, 2, 3}

    // TILE-AREA teardown (the tile object's destructor, __thiscall via ECX=area) -- NOT a chunk destructor.
    // The historical name "ChunkDestroy" was a misnomer: this is the per-TILE object whose
    // raw ADT file buffer at area+0x80 is freed here while a queued async-read completion may still
    // target it; a cancel hook retires the async object at area+0x70 before the free.
    constexpr uintptr_t kTileAreaDestroy = 0x007D6E10;
    using TileAreaDestroyFn = void(__fastcall*)(void* area);
    // Deprecated aliases (same address/field, kept so no published offset is ever deleted): the old
    // names wrongly said "chunk"; the object is the tile-area object. kOffChunkAsyncObj duplicates
    // kOffTileAsyncRead below -- it is the SAME +0x70 field of the SAME tile-area object.
    constexpr uintptr_t kChunkDestroy = kTileAreaDestroy;    // deprecated: use kTileAreaDestroy
    using ChunkDestroyFn = TileAreaDestroyFn;                // deprecated: use TileAreaDestroyFn
    constexpr size_t kOffChunkAsyncObj = 0x70;               // deprecated: use kOffTileAsyncRead
    // Near-tile placed-object counter (chunk, &progress, total) -> count of placed-object children still
    // loading that overlap the chunk box.
    constexpr uintptr_t kNearObjectCount = 0x007B50B0;

    // --- terrain draw-frustum cull ---
    // CFrustum::Cull(this=frustum, bbox[6]): __thiscall, 1 stack arg (bbox min@+0/max@+0xC), returns
    // nonzero when the box survives (visible), 0 when culled. CWorldScene::CullChunks (0x00799D40,
    // called 64x/frame -- once per slot of a sort table, each call walking that slot's own chunk list to
    // exhaustion, so this is NOT a one-call-per-chunk entry point to hook directly) calls this address
    // twice per candidate chunk: once against the chunk's primary AABB (kOffChunkBboxPrimary), and --
    // ONLY if that, an unrelated portal test, and a horizon/clip-buffer test all already passed -- a
    // second time against a narrower box (kOffChunkBboxSecondary). A chunk that survives the second call
    // is unconditionally linked into this frame's render-ready list next (a per-texture-layer-count
    // bucket sort happens after, not a further visibility gate) -- so gating a call-through detour of
    // this address on the SECOND call's own return address is the narrowest available hook that still
    // sees every chunk the terrain pass will actually draw this frame, with none of the first call's
    // false positives (chunks the second test still rejects). Declared __fastcall with a dummy edx, this
    // codebase's standard idiom for a hooked thiscall function (see kIsDrawable's own doc comment).
    constexpr uintptr_t kChunkFrustumCull = 0x009839E0;
    using ChunkFrustumCullFn = int(__fastcall*)(void* frustum, void* edx, const float* bbox);
    // Return address (call site + 5) of the second CFrustum::Cull call described above. At that instant
    // the bbox argument still on the stack IS chunk+kOffChunkBboxSecondary, so chunk = bbox - that offset
    // -- no separate chunk-identity lookup needed at the hook site.
    constexpr uintptr_t kChunkFrustumCullRetB = 0x00799E65;
    constexpr size_t    kOffChunkBboxPrimary   = 0x4C;
    constexpr size_t    kOffChunkBboxSecondary = 0x8C;

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

    // --- tile-area object fields ---
    constexpr size_t kOffTileAsyncRead  = 0x70; // async-read handle; non-zero while a tile read is in flight
    constexpr size_t kOffTileFileBuffer = 0x80; // raw ADT byte buffer; freed by kTileAreaDestroy
    constexpr size_t kOffTileFileHandle = 0x6C; // archive file handle of the open tile file (closed by async destroy)
    constexpr size_t kOffTileFileSize   = 0x84; // byte size of the +0x80 buffer
    constexpr size_t kOffTileIdxFirst   = 0x48; // first  %d of "<Map>_%d_%d.adt"
    constexpr size_t kOffTileIdxSecond  = 0x4C; // second %d of "<Map>_%d_%d.adt"

    // --- tile-area load / parse seam (used by the native split-ADT reader) ---
    // Tile-area load (__thiscall: ECX = area, one stack arg = tile filename): opens the tile file,
    // allocates the raw buffer (+0x80/+0x84) and queues the whole-file async read (+0x70) whose
    // main-thread completion is the async-load callback below, which then runs tile-area create.
    constexpr uintptr_t kTileAreaLoad = 0x007D7150;
    using TileAreaLoadFn = void(__fastcall*)(void* area, void* edx, const char* filename);
    // Tile-area create (__thiscall via ECX, no args): the monolithic top-level parser. Reads ONLY
    // MVER + the 12 MHDR offsets of the buffer at area+0x80 and stores derived pointers/counts at
    // area+0x68..+0xB8 (MCIN/MTEX/MMDX/MMID/MWMO/MWID/MDDF/MODF/MFBO/MH2O/MTXF).
    constexpr uintptr_t kTileAreaCreate = 0x007D6EF0;
    using TileAreaCreateFn = void(__fastcall*)(void* area, void* edx);
    // Native async-read completion (__cdecl, ctx = area): Create + async destroy + zero +0x70/+0x6C.
    constexpr uintptr_t kTileAreaAsyncLoadCallback = 0x007D7020;
    // Prepare-chunk (__thiscall: ECX = area, two stack args = grid row/col 0..15):
    constexpr uintptr_t kPrepareChunk = 0x007D6B30;
    using Map_PrepareChunkFn = void(__fastcall*)(void* area, void* edx, int row, int col);
    // Area update (__thiscall: ECX = area, stack args = buildFlag, uint32_t bounds[4] = {colMin,
    // rowMin, colMax, rowMax})
    constexpr uintptr_t kAreaUpdate = 0x007D6BF0;
    using Map_AreaUpdateFn = void(__fastcall*)(void* area, void* edx, int buildFlag, uint32_t* bounds);
    // Sub-chunk walk (__thiscall: ECX = chunk, one stack arg = firstBuild): the
    // SEQUENTIAL walk over the raw MCNK at chunk+0x10C that assigns the sub-chunk data
    // pointers at chunk+0x11C..+0x13C (the client never reads the MCNK-internal ofs* fields). Called
    // only by the chunk-create routine. firstBuild!=0 patches MCNR/MCAL/MCLQ size fields in place once.
    constexpr uintptr_t kChunkProcessIffChunks = 0x007C3A10;
    using ChunkProcessIffChunksFn = void(__fastcall*)(void* chunk, void* edx, int firstBuild);
    // Raw tile-buffer allocator/free pair (plain client-allocator wrappers, MapMem.cpp). The free
    // takes (ptr, size) but ignores size. The tile destructor frees +0x80 through the free half.
    constexpr uintptr_t kAllocRawAreaData = 0x007BFE40;
    using AllocRawAreaDataFn = void*(__cdecl*)(uint32_t size);
    constexpr uintptr_t kFreeRawAreaData = 0x007BFE60;
    using FreeRawAreaDataFn = void(__cdecl*)(void* buffer, uint32_t size);
    // WDT MPHD flags global (first dword of the 0x20-byte MPHD copy): bit1 = MCCV vertex format,
    // bit2 = big (4096-byte, 8-bit) MCAL. Consulted live at every alpha unpack site.
    constexpr uintptr_t kMphdFlags = 0x00CF08D0;

    // --- map low-detail (WDL) seam ---
    // Load WDL (MapLowDetail.cpp): opens "<mapPath>\<mapName>.wdl", allocates the whole file
    // into wdlState[0], then parses MVER -> optional MWMO/MWID/MODF -> MAOF -> per-tile MARE(+MAHO).
    // Convention verified against the client build directly: true __thiscall (prologue
    // 55 8B EC 81 EC 3C 01 00 00 .. 8B F9 = this out of ECX, epilogue C2 08 00 = two stack args),
    // returns 1 on success / 0 when the .wdl does not open. Single caller: the map load entry @ 0x007BFDD2
    // with ECX = kWdlState and args (&mapPath, &mapName). Declared __fastcall with a
    // dummy EDX so the trampoline routes wdlState into the this-register.
    constexpr uintptr_t kLoadWdl = 0x007CC310;
    using LoadWdlFn = uint32_t(__fastcall*)(int* wdlState, void* edx,
                                            const char* mapPath, const char* mapName);
    // The map's WDL state block (the ECX of kLoadWdl), an int[0x100A] global:
    //   [0]           raw .wdl file buffer (allocated; the unload releases it)
    //   [1..3]        MWMO data / MWID data / MODF data pointers (WMO-only maps; else stale-zero)
    //   [4]           MODF entry count (MODF size >> 6)
    //   [5]           MAOF offset table = 64*64 u32 file offsets, 0 = no low-detail tile
    //   [6..0x1005]   the 64x64 low-detail-tile pointer array (kWdlSlotCount entries)
    //   [0x1006..0x1009] the low-detail map-obj-def growable-array block
    // Zeroed whole at startup by the static ctor 0x007CC2C0, and on every map unload by
    // the unload-WDL routine 0x007CC770 (frees+zeroes every slot, zeroes [1..5]/[0x1007], releases [0]).
    // The map load entry runs the map purge routine (-> 0x007CC770 @ 0x007C3843) BEFORE kLoadWdl, so
    // the block is always clean when kLoadWdl is entered.
    constexpr uintptr_t kWdlState     = 0x00CF0900;
    constexpr uint32_t  kWdlSlotCount = 64 * 64; // dimension of the [6..] slot array (0x1000)
    // Allocate low-detail tile: pool-allocates one low-detail tile object (the per-tile low-detail
    // object stored in the kWdlState [6..] slots). Verified __cdecl, no args, pointer in EAX (prologue
    // 55 8B EC 83 EC 08 8B 15 18 54 D2 00 -- pool head at 0xD25418 -- plain C3 ret). Fields below are
    // what the kLoadWdl grid loop writes on the returned object; see AreaLow for the typed view.
    constexpr size_t kOffAreaLowMinX         = 0x04;
    constexpr size_t kOffAreaLowMinY         = 0x08;
    constexpr size_t kOffAreaLowMinZ         = 0x0C;
    constexpr size_t kOffAreaLowMaxX         = 0x10;
    constexpr size_t kOffAreaLowMaxY         = 0x14;
    constexpr size_t kOffAreaLowMaxZ         = 0x18;
    constexpr size_t kOffAreaLowCenterX      = 0x1C;
    constexpr size_t kOffAreaLowCenterY      = 0x20;
    constexpr size_t kOffAreaLowCenterZ      = 0x24;
    constexpr size_t kOffAreaLowRadius       = 0x28;
    constexpr size_t kOffAreaLowOriginX      = 0x2C;
    constexpr size_t kOffAreaLowOriginY      = 0x30;
    constexpr size_t kOffAreaLowCol          = 0x38;
    constexpr size_t kOffAreaLowRow          = 0x3C;
    constexpr size_t kOffAreaLowRenderBudget = 0x40; // byte budget for render indices, 0xC per unholed cell
    constexpr size_t kOffAreaLowMareData     = 0x44; // -> 545 s16 heightmap (17x17 outer + 16x16 inner)
    constexpr size_t kOffAreaLowMahoData     = 0x48; // -> 16 u16 hole mask, or 0
    constexpr uintptr_t kAllocAreaLow = 0x007C0A90;
    using AllocAreaLowFn = void*(__cdecl*)();
    // Free low-detail tile (landmark, not called by the core): the unload 0x007CC770 releases every
    // non-null kWdlState slot through it before zeroing the slot.
    constexpr uintptr_t kFreeAreaLow = 0x007C0C60;

    // --- runtime chunk object fields ---
    constexpr size_t kOffChunkNodeLayerCount = 0x09; // draw-node layer count
    // Chunk object -> MCNK 128-byte data header (= raw MCNK ptr + 8-byte tag). The authoritative
    // texture-layer count (SMChunk.nLayers, 0..4) lives at header + 0x0C.
    constexpr size_t kOffChunkMcnkHeader = 0x110;
    constexpr size_t kOffMcnkNLayers     = 0x0C;
    // Raw on-disk MCLY/MCAL base pointers (point into the resident MCNK block, all physical entries, not
    // just the 4 materialized layers). The 4-byte field right before the MCLY payload is its sub-chunk
    // size, so physical-layer-count = *(mclyBase - 4) / 0x10.
    constexpr size_t kOffChunkMcly       = 0x12C;
    constexpr size_t kOffChunkMcal       = 0x130;
    // The full chunk-object sub-chunk pointer block the sub-chunk walk fills (+0x11C..+0x13C). Every
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
    // Per-layer record array (4 slots, stride 0x14): +0x00 flags, +0x04 diffuse GPU texture handle,
    // +0x0C alpha GPU texture handle, +0x10 back-ptr. Only the first nLayers (<=4) records exist.
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
    // GPU device singleton; vtable + 0xA8 = the Draw (DrawIndexedPrimitive) method (batch ptr + flag).
    constexpr uintptr_t kGxDeviceSingleton = 0x00C5DF88;
    constexpr size_t    kGxDeviceDrawVtbl  = 0xA8;
    // Texture object -> GPU handle resolve.
    constexpr uintptr_t kTexResolve        = 0x004B6CB0;
    // GxRsSet / SetTexture for a sampler slot (0x15 = diffuse stage, 0x16 = alpha stage).
    constexpr uintptr_t kSetSamplerTexture = 0x00685F50;
    // Sampler addr/filter state for the just-bound texture.
    constexpr uintptr_t kSetSamplerState   = 0x00681450;
    // Lazy texture loader for one tex-owner handle slot: slot[+4] = Load(slot[+0]).
    constexpr uintptr_t kLazyLoadTexSlot   = 0x007D6980;
    // Load tile textures: builds the tile tex-owner handle array (area+0x60) from the MTEX name
    // blob -- one {name, GPU texture handle} slot per NUL-terminated name, indexed by MCLY.textureId,
    // eager-loading each through kLazyLoadTexSlot unless the archive streaming mode defers it. Native
    // this-in-ECX (the tile area) + (mtexData, mtexSize) on the stack; declared __fastcall with a dummy EDX.
    // Detoured for split tiles to source the names from the real MDID (FileDataID) instead of MTEX.
    constexpr uintptr_t kAreaLoadTextures  = 0x007D6D20;
    using Map_AreaLoadTexturesFn = void(__fastcall*)(void* area, void* edx, const void* mtexData, uint32_t mtexSize);
    // kLazyLoadTexSlot signature: __thiscall(area, slot, index); slot = { char* name; GPU texture handle tex }.
    using Map_LoadTerrainTextureFn = void(__fastcall*)(void* area, void* edx, void** slot, uint32_t index);
    // Builds the per-layer alpha texture from a layer record's MCAL into record + 0x0C.
    constexpr uintptr_t kBuildLayerAlpha   = 0x007B9DE0;
    constexpr uint32_t  kSamplerDiffuse    = 0x15;
    constexpr uint32_t  kSamplerAlpha      = 0x16;
    // Tile tex-owner: per-tile texture-handle array, indexed by MCLY.textureId, stride 8
    // ([+0] = MTEX filename ptr, [+4] = loaded GPU texture handle). Covers the whole tile MTEX set.
    constexpr size_t kOffTexOwnerHandleArray = 0x60;
    constexpr size_t kTexOwnerHandleStride   = 0x08;

    // --- terrain height blend ---
    // Shader-path per-chunk draw signature: native this-in-ECX, no stack args. Declared __fastcall
    // with a dummy EDX so the trampoline routes the chunk into the this-register.
    using Map_SurfaceChunkDrawShaderFn = void(__fastcall*)(void* chunkObj, void* edx);
    // ACTIVE terrain pixel-shader table: GPU shader handle[4], one slot per layer count 1..4. Rewritten
    // once per bucket per frame by the draw-node shader setter (0x007D3E10) and bound at GxRs 0x4E by
    // the bucket loop BEFORE the per-chunk draw leaf runs -- so at kSurfaceChunkDrawShader time,
    // slot[nLayers-1] is the stock wrapper the pending DIP will use. A detour that swaps GxRs 0x4E to
    // its own wrapper before the original leaf (and restores after) owns that one chunk's PS.
    constexpr uintptr_t kActiveTerrainPs = 0x00D1D080;
    // Pixel-shader terrain-path byte gate: non-zero when the pixel-shader terrain path is
    // active (the only path kSurfaceChunkDrawShader runs under).
    constexpr uintptr_t kEnableTerrainShaderPixel = 0x00CE049E;
    // GxRs state indices for the deferred shader binds (same setter as kSetSamplerTexture): the
    // bucket loop writes the terrain PS wrapper at 0x4E; the pre-draw GxState flush applies the
    // wrapper's live handle (+0x20, created flag +0x30) to the device. Mirrors
    // offsets/engine/Shader.hpp kStateVertexShader/kStatePixelShader.
    constexpr uint32_t kGxStateVertexShader = 0x4D;
    constexpr uint32_t kGxStatePixelShader  = 0x4E;
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
    constexpr size_t kOffChunkNodeChunk   = 0x10; // chunk object backing the node
    constexpr size_t kOffChunkNodeAlphaRT = 0x84; // combined alpha texture handle bound at s(nLayers)
    // Layer record fields (record = node + kOffChunkLayerRecords + i*kChunkLayerRecordStride).
    constexpr size_t kOffLayerSlotFlags = 0x00;   // u16 MCLY flags low word (anim dir/speed/animate)
    constexpr size_t kOffLayerSlotIndex = 0x02;   // u16 slot index (0..3)
    constexpr size_t kOffLayerSlotTexId = 0x08;   // u32 MCLY textureId (dedup key only at draw time)
    constexpr size_t kOffLayerSlotAlpha = 0x0C;   // per-layer alpha handle; 0 on the shader path
    constexpr size_t kOffLayerSlotNode  = 0x10;   // back-pointer to the node
    // Chunk-object identity fields (engine-written, not data-trusted).
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
    // is the chunk object (layer count at kOffChunkNodeLayerCount, layer slots at kOffChunkLayerRecords).
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
    // Chunk build: native this-in-ECX (__thiscall, ret 8). Declared __fastcall with a dummy EDX so the
    // trampoline routes the chunk into the this-register and keeps the two stack args.
    using Map_ChunkBuildFn = void(__fastcall*)(void* chunk, void* edx, void* rawMcnk, int param2);

    // --- typed views over the objects above ---
    // The constants are the curated landmarks; these structs give named, typed access to the same fields,
    // with every member offset checked against a constant at compile time (a wrong padding fails the build).
    // Only known fields are named; the gaps are explicit padding. Pointers are 4 bytes on the 32-bit client.
#pragma pack(push, 1)
    /**
     * @brief Tile-area object (one per resident map tile): filename index, file handle, async-read
     *        state, file buffer.
     *
     * Pointer-valued fields are stored as uint32_t, not void*: with more than one such field in the same
     * struct, sizeof(void*) would drive the padding between them, and this header is 32/64-bit-neutral
     * (sizeof(uint32_t) is not). Only ever the LAST field of a struct is safe to type as a real pointer.
     */
    struct TileArea
    {
        uint8_t  _pad00[kOffTileIdxFirst];
        int32_t  tileFirst;        // kOffTileIdxFirst  (first  %d of "<Map>_%d_%d.adt")
        int32_t  tileSecond;       // kOffTileIdxSecond (second %d of "<Map>_%d_%d.adt")
        uint8_t  _pad50[kOffTileFileHandle - (kOffTileIdxSecond + sizeof(int32_t))];
        uint32_t fileHandle;       // kOffTileFileHandle (archive file handle of the open tile file)
        uint32_t asyncRead;        // kOffTileAsyncRead  (non-zero while the root read is in flight)
        uint8_t  _pad74[kOffTileFileBuffer - (kOffTileAsyncRead + sizeof(uint32_t))];
        uint32_t fileBuffer;       // kOffTileFileBuffer (non-zero once the file buffer is allocated)
        uint32_t fileSize;         // kOffTileFileSize   (byte size of the +0x80 buffer)
    };
    static_assert(offsetof(TileArea, tileFirst)  == kOffTileIdxFirst,  "TileArea.tileFirst");
    static_assert(offsetof(TileArea, tileSecond) == kOffTileIdxSecond, "TileArea.tileSecond");
    static_assert(offsetof(TileArea, fileHandle) == kOffTileFileHandle, "TileArea.fileHandle");
    static_assert(offsetof(TileArea, asyncRead)  == kOffTileAsyncRead,  "TileArea.asyncRead");
    static_assert(offsetof(TileArea, fileBuffer) == kOffTileFileBuffer, "TileArea.fileBuffer");
    static_assert(offsetof(TileArea, fileSize)   == kOffTileFileSize,   "TileArea.fileSize");

    /**
     * @brief Runtime chunk object: tex-owner link, local grid index, and the sub-chunk
     *        pointer block the sub-chunk walk fills (raw MCNK, header, and each parsed sub-chunk).
     *
     * The old single struct conflated two objects: nodeLayerCount @0x09 is a draw-node field, while
     * everything here is a chunk-object field. They are now two typed views --
     * MapChunk for the chunk object, RenderNode for the draw node reached via chunk+0xA8.
     */
    struct MapChunk
    {
        uint8_t  _pad00[kOffChunkTexOwnerSrc];
        uint32_t texOwnerSrc;      // kOffChunkTexOwnerSrc (tagged link; tile tex-owner = (v & ~1) + 8)
        int32_t  indexX;           // kOffMapChunkIndexX (local 0..15, MCIN slot = y*16 + x)
        int32_t  indexY;           // kOffMapChunkIndexY
        uint8_t  _pad2C[kOffChunkRawMcnk - (kOffMapChunkIndexY + sizeof(int32_t))];
        uint32_t rawMcnk;          // kOffChunkRawMcnk    (raw MCNK tag+size header in the tile buffer)
        uint32_t mcnkHeader;       // kOffChunkMcnkHeader -> McnkHeader (raw MCNK ptr + 8-byte tag)
        uint8_t  _pad114[kOffChunkMcvt - (kOffChunkMcnkHeader + sizeof(uint32_t))];
        uint32_t mcvt;             // kOffChunkMcvt (145 floats, relative heights)
        uint32_t mccv;             // kOffChunkMccv (145 x BGRA vertex colors, vertex format 2 only)
        uint32_t mcnr;             // kOffChunkMcnr (435 signed normal bytes)
        uint32_t mcsh;             // kOffChunkMcsh (512-byte shadow bitmap, header flags bit0 gates use)
        uint32_t mcly;             // kOffChunkMcly (raw on-disk layer records)
        uint32_t mcal;             // kOffChunkMcal (raw on-disk alpha maps)
        uint32_t mcrf;             // kOffChunkMcrf (u32 refs: doodads first, then wmos)
        uint32_t mclq;             // kOffChunkMclq (legacy liquid layers, header sizeLiquid > 8 gates)
        uint32_t mcse;             // kOffChunkMcse (sound emitters, header nSndEmitters gates)
    };
    static_assert(offsetof(MapChunk, texOwnerSrc) == kOffChunkTexOwnerSrc,  "MapChunk.texOwnerSrc");
    static_assert(offsetof(MapChunk, indexX)      == kOffMapChunkIndexX,   "MapChunk.indexX");
    static_assert(offsetof(MapChunk, indexY)      == kOffMapChunkIndexY,   "MapChunk.indexY");
    static_assert(offsetof(MapChunk, rawMcnk)     == kOffChunkRawMcnk,    "MapChunk.rawMcnk");
    static_assert(offsetof(MapChunk, mcnkHeader)  == kOffChunkMcnkHeader, "MapChunk.mcnkHeader");
    static_assert(offsetof(MapChunk, mcvt)        == kOffChunkMcvt,       "MapChunk.mcvt");
    static_assert(offsetof(MapChunk, mccv)        == kOffChunkMccv,       "MapChunk.mccv");
    static_assert(offsetof(MapChunk, mcnr)        == kOffChunkMcnr,       "MapChunk.mcnr");
    static_assert(offsetof(MapChunk, mcsh)        == kOffChunkMcsh,       "MapChunk.mcsh");
    static_assert(offsetof(MapChunk, mcly)        == kOffChunkMcly,       "MapChunk.mcly");
    static_assert(offsetof(MapChunk, mcal)        == kOffChunkMcal,       "MapChunk.mcal");
    static_assert(offsetof(MapChunk, mcrf)        == kOffChunkMcrf,       "MapChunk.mcrf");
    static_assert(offsetof(MapChunk, mclq)        == kOffChunkMclq,       "MapChunk.mclq");
    static_assert(offsetof(MapChunk, mcse)        == kOffChunkMcse,       "MapChunk.mcse");

    /**
     * @brief One MCLY layer slot inside a draw node's layer array
     *        (record = node + kOffChunkLayerRecords + i*kChunkLayerRecordStride).
     */
    struct LayerRecord
    {
        uint8_t  _pad00[kOffLayerSlotTexId];
        uint32_t texId;    // kOffLayerSlotTexId (MCLY textureId, dedup key only at draw time)
        uint8_t  _pad0C[kChunkLayerRecordStride - (kOffLayerSlotTexId + sizeof(uint32_t))];
    };
    static_assert(offsetof(LayerRecord, texId) == kOffLayerSlotTexId,   "LayerRecord.texId");
    static_assert(sizeof(LayerRecord)          == kChunkLayerRecordStride, "LayerRecord size/stride");

    /** @brief Draw node (chunk+0xA8): layer count/flags, owning chunk, layer array. */
    struct RenderNode
    {
        uint8_t     _pad00[kOffChunkNodeLayerCount];
        uint8_t     nodeLayerCount;   // kOffChunkNodeLayerCount (draw-node layer count)
        uint16_t    nodeFlags;        // kOffChunkNodeFlags (bit0 = mask-family layer, bit2 = cube-env layer)
        uint8_t     _pad0C[kOffChunkNodeChunk - (kOffChunkNodeFlags + sizeof(uint16_t))];
        uint32_t    nodeChunk;        // kOffChunkNodeChunk -> MapChunk backing the node
        uint8_t     _pad14[kOffChunkLayerRecords - (kOffChunkNodeChunk + sizeof(uint32_t))];
        LayerRecord layers[4];        // kOffChunkLayerRecords; only [0..nodeLayerCount) are valid
    };
    static_assert(offsetof(RenderNode, nodeLayerCount) == kOffChunkNodeLayerCount, "RenderNode.nodeLayerCount");
    static_assert(offsetof(RenderNode, nodeFlags)      == kOffChunkNodeFlags,      "RenderNode.nodeFlags");
    static_assert(offsetof(RenderNode, nodeChunk)      == kOffChunkNodeChunk,      "RenderNode.nodeChunk");
    static_assert(offsetof(RenderNode, layers)         == kOffChunkLayerRecords,   "RenderNode.layers");
    static_assert(offsetof(RenderNode, layers) + sizeof(RenderNode::layers) == kOffChunkNodeAlphaRT,
                  "RenderNode.layers should end exactly at the combined alpha-RT slot");

    /** @brief MCNK 128-byte data header (chunk->mcnkHeader): the authoritative texture-layer count. */
    struct McnkHeader
    {
        uint8_t  _pad00[kOffMcnkNLayers];
        uint32_t nLayers;          // kOffMcnkNLayers (SMChunk.nLayers, 0..4)
    };
    static_assert(offsetof(McnkHeader, nLayers) == kOffMcnkNLayers, "McnkHeader.nLayers");

    /**
     * @brief Low-detail tile object (from the low-detail-tile allocator): the WDL grid-loop bounds,
     *        column/row, render-index budget, and MARE/MAHO data.
     */
    struct AreaLow
    {
        uint8_t  _pad00[kOffAreaLowMinX];
        float    minX;
        float    minY;
        float    minZ;
        float    maxX;
        float    maxY;
        float    maxZ;
        float    centerX;
        float    centerY;
        float    centerZ;
        float    radius;
        float    originX;
        float    originY;
        uint8_t  _pad34[kOffAreaLowCol - (kOffAreaLowOriginY + sizeof(float))];
        int32_t  col;               // kOffAreaLowCol
        int32_t  row;               // kOffAreaLowRow
        uint32_t renderBudget;      // kOffAreaLowRenderBudget
        uint32_t mareData;          // kOffAreaLowMareData (raw address; see MapChunk's note on why)
        uint32_t mahoData;          // kOffAreaLowMahoData (raw address, or 0)
    };
    static_assert(offsetof(AreaLow, minX)         == kOffAreaLowMinX,         "AreaLow.minX");
    static_assert(offsetof(AreaLow, minY)         == kOffAreaLowMinY,         "AreaLow.minY");
    static_assert(offsetof(AreaLow, minZ)         == kOffAreaLowMinZ,         "AreaLow.minZ");
    static_assert(offsetof(AreaLow, maxX)         == kOffAreaLowMaxX,         "AreaLow.maxX");
    static_assert(offsetof(AreaLow, maxY)         == kOffAreaLowMaxY,         "AreaLow.maxY");
    static_assert(offsetof(AreaLow, maxZ)         == kOffAreaLowMaxZ,         "AreaLow.maxZ");
    static_assert(offsetof(AreaLow, centerX)      == kOffAreaLowCenterX,      "AreaLow.centerX");
    static_assert(offsetof(AreaLow, centerY)      == kOffAreaLowCenterY,      "AreaLow.centerY");
    static_assert(offsetof(AreaLow, centerZ)      == kOffAreaLowCenterZ,      "AreaLow.centerZ");
    static_assert(offsetof(AreaLow, radius)       == kOffAreaLowRadius,       "AreaLow.radius");
    static_assert(offsetof(AreaLow, originX)      == kOffAreaLowOriginX,      "AreaLow.originX");
    static_assert(offsetof(AreaLow, originY)      == kOffAreaLowOriginY,      "AreaLow.originY");
    static_assert(offsetof(AreaLow, col)          == kOffAreaLowCol,          "AreaLow.col");
    static_assert(offsetof(AreaLow, row)          == kOffAreaLowRow,          "AreaLow.row");
    static_assert(offsetof(AreaLow, renderBudget) == kOffAreaLowRenderBudget, "AreaLow.renderBudget");
    static_assert(offsetof(AreaLow, mareData)     == kOffAreaLowMareData,     "AreaLow.mareData");
    static_assert(offsetof(AreaLow, mahoData)     == kOffAreaLowMahoData,     "AreaLow.mahoData");
#pragma pack(pop)

    // Chunk visibility and the low-detail (WDL) horizon
    /// Resolves which liquid the camera is in/under each frame -- the hook for underwater state, fog
    /// and camera-liquid effects driven by terrain water. __cdecl, caller-cleaned.
    constexpr uintptr_t kUpdateViewerLiquid                = 0x00790920;
    /// The only per-frame consumer of the WDL low-detail tile grid -- the hook for changing how (or
    /// whether) the distant horizon terrain is culled and drawn. __cdecl, caller-cleaned.
    constexpr uintptr_t kCullHorizon                       = 0x00791980;
    /// The visibility gate for terrain liquid instances specifically, separate from the terrain-surface
    /// cull. __cdecl, caller-cleaned.
    constexpr uintptr_t kCullLiquidChunks                  = 0x007935A0;
    /// Owns the per-slot chunk visibility walk and the render-ready link -- a detour can add chunks to,
    /// or remove them from, this frame's terrain list wholesale. __cdecl, caller-cleaned.
    constexpr uintptr_t kCullChunks                        = 0x00799D40;

    // Ground effects / detail doodads
    /// Brackets the entire ground-effect draw pass; state set here is the one place that affects every
    /// clutter instance in the frame. __cdecl, caller-cleaned.
    constexpr uintptr_t kRenderDetailDoodads               = 0x007984A0;
    /// Builds the distance alpha-ramp texture that fades clutter out -- detour to change the ground-
    /// effect fade curve. __cdecl, caller-cleaned.
    constexpr uintptr_t kCreateDetailDoodadAlphaRamp       = 0x007B11B0;
    /// Teardown of the detail-doodad model set, symmetric with the model resolve above. __cdecl,
    /// caller-cleaned.
    constexpr uintptr_t kDestroyDetailDoodadModels         = 0x007B1380;
    /// The ground-effect subsystem init (pools, heaps, shader handles) -- a place to enlarge the
    /// detail-doodad budget before anything allocates. __cdecl, caller-cleaned.
    constexpr uintptr_t kInitDetailDoodads                 = 0x007B2760;
    /// Per-frame rebuild of the detail-doodad vertex/index pools, gated on the dirty flag at 0x00D1C4C0
    /// -- hook to instrument or resize the clutter pools. __cdecl, caller-cleaned.
    constexpr uintptr_t kUpdateDetailDoodadPools           = 0x007B2A80;
    /// The ground-effect render state and shader selection block -- the place to substitute a custom
    /// detail-doodad shader. __cdecl, caller-cleaned.
    constexpr uintptr_t kSetupDetailDoodadRenderState      = 0x007B2D30;
    /// The per-detail-doodad asset load, where the model path is built and requested. __thiscall,
    /// caller-cleaned.
    constexpr uintptr_t kLoadDetailDoodadData              = 0x007B3050;
    /// The leaf that places one clutter instance (position, scale, rotation, colour) -- the finest-
    /// grain hook for ground-effect placement. __thiscall, 7 stack args.
    constexpr uintptr_t kAddDetailDoodadInstance           = 0x007B31E0;
    /// Index-to-model resolution for detail doodads -- one detour redirects every ground-effect model
    /// lookup. __cdecl, caller-cleaned.
    constexpr uintptr_t kResolveDetailDoodadModel          = 0x007B3530;
    /// Resolves the doodad model set a chunk's ground effects need -- the seam for substituting modern
    /// detail-doodad models. __thiscall, caller-cleaned.
    constexpr uintptr_t kLoadChunkDetailDoodadModels       = 0x007D05F0;
    /// The whole ground-effect placement for one chunk (GroundEffectTexture/Doodad lookup, density,
    /// per-cell scatter) -- the hook for custom or denser ground clutter. __thiscall, caller-cleaned.
    constexpr uintptr_t kBuildChunkDetailDoodads           = 0x007D3390;
    /// The visibility-driven "spawn this chunk's detail-doodad instance" gate -- hook to control
    /// ground-effect pop-in per chunk. __thiscall, caller-cleaned.
    constexpr uintptr_t kEnsureChunkDetailDoodadInst       = 0x007D3FE0;

    // Per-chunk terrain draw and the terrain render passes
    /// Brackets the solid terrain sub-pass, so an extension can add its own full-terrain overlay pass
    /// at the right point in the frame. __cdecl, caller-cleaned.
    constexpr uintptr_t kRenderChunksSolid                 = 0x00793B10;
    /// An already-wired, normally-inert per-chunk overlay pass an extension can take over for zone/area
    /// visualization without adding a pass of its own. __cdecl, caller-cleaned.
    constexpr uintptr_t kRenderChunksZoneDebug             = 0x00793C30;
    /// The bucketed single-pass terrain loop (the one that binds the terrain pixel shader per layer-
    /// count bucket) -- hook to reorder or filter buckets. __cdecl, caller-cleaned.
    constexpr uintptr_t kRenderChunksSinglePass            = 0x007989C0;
    /// The whole terrain pass in one bracket -- set up and restore render state, or skip terrain
    /// entirely, around every chunk of the frame. __cdecl, caller-cleaned.
    constexpr uintptr_t kRenderChunks                      = 0x00798DA0;
    /// Builds the shared terrain vertex-shader constant block at 0x00D250A0 once per pass -- the place
    /// to reserve or repurpose terrain VS registers safely. __cdecl, caller-cleaned.
    constexpr uintptr_t kInitTerrainVertexShaderConstants  = 0x007CFBE0;
    /// Where a chunk's vertex and index buffers are locked/filled for the frame -- the seam for
    /// injecting extra vertex attributes. __thiscall, 2 stack args.
    constexpr uintptr_t kPrepareChunkBuffers               = 0x007D02C0;
    /// The branch that decides a chunk uses the shared streaming buffer pool instead of its own block
    /// -- hook to force one buffering strategy. __thiscall, caller-cleaned.
    constexpr uintptr_t kUseStreamingChunkBuffers          = 0x007D0420;
    /// Called from all three terrain passes, so it is the one hook that sees every chunk about to be
    /// drawn regardless of which pass is active. __thiscall, 1 stack arg.
    constexpr uintptr_t kSetupChunkRender                  = 0x007D04A0;
    /// The untextured/solid terrain draw body (zone-fill and debug overlays), a clean place to draw
    /// per-chunk overlays in world space. __thiscall, caller-cleaned.
    constexpr uintptr_t kDrawChunkSolid                    = 0x007D3010;
    /// Shader-path counterpart of the solid draw -- needed so an overlay extension behaves identically
    /// on both graphics paths. __thiscall, caller-cleaned.
    constexpr uintptr_t kDrawChunkSolidShader              = 0x007D3240;
    /// The last per-chunk seam before the chunk is handed to a draw body -- layers, buffers and loaded-
    /// state are finalized here. __thiscall, caller-cleaned.
    constexpr uintptr_t kPrepareChunkRender                = 0x007D3F70;

    // Terrain CVar callbacks (callback-slot targets, no CALL xrefs by design)
    /// Intercept terrain LOD changes (it writes the enable bits at 0x00CD774C) so an extension can pin
    /// or extend terrain detail levels. __cdecl, caller-cleaned.
    constexpr uintptr_t kTerrainLodCallback                = 0x0078D610;
    /// Intercept the terrain-shadow enable bit so a custom shadow path can own that toggle. __cdecl,
    /// caller-cleaned.
    constexpr uintptr_t kTerrainShadowsCallback            = 0x0078D660;
    /// The toggle that selects 4444 vs 8888 MCAL alpha textures -- hook it to force the bit depth a
    /// modern alpha source needs. __cdecl, caller-cleaned.
    constexpr uintptr_t kTerrainAlphaBitDepthCallback      = 0x0078DA50;
    /// Intercept the clutter density clamp before it reaches the known density-clamp immediates,
    /// allowing densities the stock UI cannot express. __cdecl, caller-cleaned.
    constexpr uintptr_t kGroundEffectDensityCallback       = 0x0078DAB0;
    /// Intercept the clutter draw-distance clamp (it feeds the World ground-effect distance globals),
    /// the companion to the density callback. __cdecl, caller-cleaned.
    constexpr uintptr_t kGroundEffectDistCallback          = 0x0078DB10;

    // Terrain chunk build (MCVT / MCNR / MCLY / MCRF / MCSE / index+vertex buffers)
    /// Fires when a resident chunk releases its GPU/pool resources -- the correct place to release
    /// extension resources keyed to that chunk. __thiscall, caller-cleaned.
    constexpr uintptr_t kPurgeChunk                        = 0x007C3370;
    /// Owns the triangle index emission and therefore the MCNK hole mask -- detour it to add, remove or
    /// reshape terrain holes at build time. __thiscall, 2 stack args.
    constexpr uintptr_t kBuildChunkIndices                 = 0x007C3B60;
    /// Rewrite the 145-entry XY template grid at 0x00D25498 that every chunk's vertex build indexes,
    /// i.e. change terrain vertex layout globally in one place. __cdecl, caller-cleaned.
    constexpr uintptr_t kInitChunkVertexTable              = 0x007C3C60;
    /// One-shot seam to install terrain-wide state (the geo-to-tex ratio at 0x00D25488 and the render-
    /// chunk pool init) before any tile is parsed. __cdecl, caller-cleaned.
    constexpr uintptr_t kInitTerrainChunkSystem            = 0x007C3D90;
    /// Post-hook to rewrite the world-space MCVT heights actually written into the vertex buffer
    /// (terrain deformation, height remap) without touching the file data. __thiscall, 2 stack args.
    constexpr uintptr_t kBuildChunkVerticesWorldHigh       = 0x007C3F30;
    /// Same as the high variant but for the reduced-detail vertex format, so a height override stays
    /// consistent when terrain LOD drops. __thiscall, 2 stack args.
    constexpr uintptr_t kBuildChunkVerticesWorldLow        = 0x007C4620;
    /// Intercept the chunk-local (instanced-transform) vertex build used by the streaming buffer path.
    /// __thiscall, 1 stack arg.
    constexpr uintptr_t kBuildChunkVerticesLocalHigh       = 0x007C4960;
    /// Low-detail counterpart of the local vertex build; needed to keep a vertex-format extension
    /// complete across all four builders. __thiscall, 1 stack arg.
    constexpr uintptr_t kBuildChunkVerticesLocalLow        = 0x007C4F10;
    /// The index-range/primitive-count wrapper that feeds the draw batch; a detour can retarget a
    /// chunk's index sub-range (partial-chunk draws, LOD stitching). __stdcall, 2 stack args.
    constexpr uintptr_t kBuildChunkIndexRange              = 0x007C51B0;
    /// Post-hook to widen or replace the chunk AABB and bounding sphere that the frustum cull and the
    /// intersect queries both consume. __thiscall, caller-cleaned.
    constexpr uintptr_t kBuildChunkBounds                  = 0x007C5220;
    /// The single dispatcher that picks which of the four vertex builders runs -- detour it to force
    /// one vertex format (world/local, high/low) for all terrain. __stdcall, 3 stack args.
    constexpr uintptr_t kBuildChunkVertices                = 0x007C54C0;
    /// The single builder that turns both MCLQ and normalized MH2O into live liquid instances -- the
    /// place to add a liquid layer or override its type/height. __thiscall, 1 stack arg.
    constexpr uintptr_t kBuildChunkLiquids                 = 0x007C5690;
    /// Earliest per-chunk seam -- attach extension-owned side data to a chunk object the moment it
    /// exists, before any parse. __thiscall, caller-cleaned.
    constexpr uintptr_t kConstructChunk                    = 0x007C5C50;
    /// Symmetric teardown point for anything attached at construct time; also the last moment a chunk's
    /// sub-chunk pointers are still valid. __thiscall, caller-cleaned.
    constexpr uintptr_t kDestroyChunk                      = 0x007C5E50;
    /// The MCSE consumer -- an extension can inject or suppress per-chunk ambient sound emitters.
    /// __thiscall, 1 stack arg.
    constexpr uintptr_t kBuildChunkSoundEmitters           = 0x007C6060;
    /// The MCRF walk that spawns a chunk's doodad and map-object references -- detour to filter, add or
    /// reroute per-chunk placements. __thiscall, 4 stack args.
    constexpr uintptr_t kBuildChunkRefs                    = 0x007C6150;
    /// Hook to re-select or inject the dynamic lights a terrain chunk considers when a light moves.
    /// __thiscall, caller-cleaned.
    constexpr uintptr_t kUpdateChunkLights                 = 0x007C65A0;

    // Terrain liquid geometry (MCLQ / MH2O instances)
    /// The distance-driven decision to drop a liquid instance's GPU resources -- hook to pin water
    /// geometry resident. __thiscall, caller-cleaned.
    constexpr uintptr_t kUpdateChunkLiquidPurge            = 0x007CDE30;
    /// The AABB the liquid cull and the liquid raycast both use -- widen it and custom water stops
    /// being culled early. __thiscall, 1 stack arg.
    constexpr uintptr_t kGetChunkLiquidBounds              = 0x007CDE80;
    /// Computes the liquid instance's XY vertex extents from the owning chunk -- the place to change
    /// water tile granularity. __thiscall, caller-cleaned.
    constexpr uintptr_t kBuildChunkLiquidVertexGrid        = 0x007CDF80;
    /// The authoritative per-instance water surface height sampler behind swim/submersion checks --
    /// detour to reshape water levels. __thiscall, 3 stack args.
    constexpr uintptr_t kGetChunkLiquidSurfaceHeight       = 0x007CE0B0;
    /// The per-tile liquid existence bitmap test used by height, intersect and build alike -- one
    /// detour changes water coverage everywhere consistently. __thiscall, 2 stack args.
    constexpr uintptr_t kChunkLiquidTileExists             = 0x007CE1F0;
    /// Emits water collision triangles -- the seam for making custom liquid surfaces
    /// collidable/raycastable. __thiscall, 4 stack args.
    constexpr uintptr_t kGetChunkLiquidTris                = 0x007CE5D0;
    /// Earliest per-liquid-instance seam for attaching extension state (matched by the pooled free at
    /// 0x007C04A0 below). __thiscall, caller-cleaned.
    constexpr uintptr_t kConstructChunkLiquid              = 0x007CEE10;
    /// Builds the liquid draw batch (vertices, indices, material) for one chunk -- the seam for custom
    /// water tessellation or material selection. __thiscall, caller-cleaned.
    constexpr uintptr_t kBatchChunkLiquid                  = 0x007CF200;
    /// Pairs with the create above; the point where a terrain buffer block returns to the free list.
    /// __cdecl, caller-cleaned.
    constexpr uintptr_t kFreeChunkBuffer                   = 0x007CF790;
    /// The lazy "build my batch if I don't have one" gate -- hook to invalidate or force-rebuild a
    /// chunk's water geometry at will. __thiscall, caller-cleaned.
    constexpr uintptr_t kPrepareChunkLiquidRender          = 0x007CF9A0;

    // Terrain queries: area id, ground type, shadow, liquid, height and collision
    /// Walks a tile's chunks to find nearby liquid for ambient sound emitters -- the hook for custom
    /// water ambience. __thiscall, 3 stack args.
    constexpr uintptr_t kQueryChunkLiquidSounds            = 0x0079C360;
    /// Resolves a world position to the MCNK areaId -- the hook for remapping zones/subzones per chunk
    /// without editing the ADT. __cdecl, caller-cleaned.
    constexpr uintptr_t kQueryChunkAreaId                  = 0x007A0490;
    /// Maps a position to the chunk's terrain type (footstep sounds, footprints) -- hook to add ground
    /// types the DBC does not have. __cdecl, caller-cleaned.
    constexpr uintptr_t kQueryGroundType                   = 0x007A0530;
    /// Samples the baked MCSH terrain shadow at a position -- the hook for feeding a custom shadow
    /// source to gameplay/lighting consumers. __cdecl, caller-cleaned.
    constexpr uintptr_t kQueryTerrainShadow                = 0x007A06A0;
    /// The terrain-side liquid probe (type + surface height at a point) that feeds swim state and
    /// liquid status. __cdecl, caller-cleaned.
    constexpr uintptr_t kQueryTerrainLiquid                = 0x007A0820;
    /// The combined terrain+map-object liquid status resolver -- one detour changes what the whole
    /// client thinks is water at a point. __cdecl, caller-cleaned.
    constexpr uintptr_t kQueryLiquidStatus                 = 0x007A0B00;
    /// The terrain ray/segment intersect entry -- hook to add or veto terrain hits for picking, camera
    /// collision and line-of-sight. __cdecl, caller-cleaned.
    constexpr uintptr_t kIntersectTerrain                  = 0x007A39F0;
    /// The finest terrain collision granularity (a single MCVT cell's two triangles) -- the exact place
    /// to inject sub-chunk height overrides. __cdecl, caller-cleaned.
    constexpr uintptr_t kGetSubChunkTri                    = 0x007A6260;
    /// Per-chunk triangle emission, the level below the terrain-wide collector; hook to alter one
    /// chunk's collision mesh. __cdecl, caller-cleaned.
    constexpr uintptr_t kGetChunkTris                      = 0x007A6630;
    /// Collects terrain triangles in a region -- the entry an extension detours to expose modified
    /// terrain geometry to physics/pathing consumers. __cdecl, caller-cleaned.
    constexpr uintptr_t kGetTerrainTris                    = 0x007A6830;
    /// Snaps a world object onto the terrain sub-chunk under it -- the hook for custom ground-snapping
    /// behavior. __cdecl, caller-cleaned.
    constexpr uintptr_t kSnapObjectToSubChunk              = 0x007B4A50;
    /// Position-to-chunk resolution used when linking static entities into the terrain grid, distinct
    /// from the already-known chunk lookup. __cdecl, caller-cleaned.
    constexpr uintptr_t kLinkEntityGetChunk                = 0x007C1660;
    /// The cheapest of the three chunk intersect entries -- a good early-out hook for terrain hit
    /// filtering. __thiscall, 4 stack args.
    constexpr uintptr_t kIntersectChunkBox                 = 0x007D8730;
    /// Per-chunk triangle-level intersect; a detour here can substitute custom collision triangles for
    /// one chunk. __thiscall, 3 stack args.
    constexpr uintptr_t kIntersectChunkRay                 = 0x007D8840;
    /// The volume/box variant of the per-chunk intersect, needed alongside the ray variant for complete
    /// collision coverage. __thiscall, 3 stack args.
    constexpr uintptr_t kIntersectChunkVolume              = 0x007D8E00;

    // Terrain shadow map bind
    /// Binds the projected shadow-map textures and constants for the terrain pass specifically -- the
    /// hook for a custom terrain shadowing scheme. __cdecl, caller-cleaned.
    constexpr uintptr_t kBindTerrainShadowMap              = 0x00874660;

    // Texture layers, alpha maps and terrain shadow maps (render-chunk side)
    /// Teardown counterpart of the layer build -- release any extension-side per-layer resource exactly
    /// when the client does. __thiscall, caller-cleaned.
    constexpr uintptr_t kFreeChunkLayers                   = 0x007B7350;
    /// The "are all my layer textures resident yet" gate -- a detour can hold a chunk back or force it
    /// ready while streaming a modern texture set. __thiscall, caller-cleaned.
    constexpr uintptr_t kUpdateChunkLayersLoaded           = 0x007B73E0;
    /// Decides which lights reach a terrain chunk's constant block -- the hook for custom terrain
    /// lighting selection. __stdcall, 1 stack arg.
    constexpr uintptr_t kSelectChunkLights                 = 0x007B7BD0;
    /// The combined alpha+MCSH unpack -- the single place to change how the baked terrain shadow bitmap
    /// is folded into the alpha channel. __thiscall, 9 stack args.
    constexpr uintptr_t kUnpackLayerAlphaShadowBits        = 0x007B87F0;
    /// The format dispatcher for MCAL decompression (4444/8888, mip0/mip1, fixed/unfixed) -- one detour
    /// serves every alpha bit depth from a modern source. __thiscall, 6 stack args.
    constexpr uintptr_t kUnpackLayerAlphaBits              = 0x007B8E20;
    /// Per-layer seam -- swap the resolved diffuse texture or the layer flags for exactly one MCLY
    /// entry. __thiscall, 3 stack args.
    constexpr uintptr_t kCreateChunkLayer                  = 0x007B9250;
    /// Attach extension state to the draw node (the object holding layer count, flags, layer records
    /// and the alpha RT) at creation. __thiscall, caller-cleaned.
    constexpr uintptr_t kConstructRenderChunk              = 0x007B9690;
    /// The per-chunk entry that materializes all MCLY layer slots -- a detour can add, reorder or drop
    /// texture layers before any alpha map is unpacked. __thiscall, caller-cleaned.
    constexpr uintptr_t kCreateChunkLayers                 = 0x007B9770;
    /// The point where a chunk hands its vertex/index block back to the pooled allocator -- hook to
    /// track or retain terrain GPU buffers. __thiscall, caller-cleaned.
    constexpr uintptr_t kFreeRenderChunkBuf                = 0x007B9830;
    /// Owns the per-layer alpha texture creation path (the one that branches on the big-MCAL MPHD bit),
    /// so an extension can serve a higher-resolution alpha per layer. __thiscall, 1 stack arg.
    constexpr uintptr_t kCreateChunkLayerTexture           = 0x007B9890;
    /// MCSH-only path; a detour can substitute or disable the baked per-chunk shadow bitmap
    /// independently of the alpha layers. __thiscall, caller-cleaned.
    constexpr uintptr_t kUnpackChunkShadowBits             = 0x007B9950;
    /// Builds the combined 4-channel alpha render target the pixel-shader terrain path samples -- the
    /// hook for replacing the packed alpha atlas wholesale. __thiscall, caller-cleaned.
    constexpr uintptr_t kCreateChunkShaderTexture          = 0x007B99B0;
    /// Symmetric teardown for draw-node-keyed extension state. __thiscall, caller-cleaned.
    constexpr uintptr_t kDestroyRenderChunk                = 0x007B9D60;
    /// Control the format/size of the per-chunk shadow texture (handle lives at renderChunk+0x88).
    /// __thiscall, caller-cleaned.
    constexpr uintptr_t kAllocChunkShadowTexture           = 0x007B9EE0;
    /// Control the combined alpha render target (renderChunk+0x84) that the shader terrain path binds
    /// -- e.g. raise its resolution. __thiscall, caller-cleaned.
    constexpr uintptr_t kAllocChunkShaderTexture           = 0x007B9F90;
    /// The lazy, draw-time allocator for a chunk's layer textures -- the seam where an extension can
    /// force allocation of extra layer slots. __thiscall, caller-cleaned.
    constexpr uintptr_t kAllocChunkLayerTextures           = 0x007BA050;
    /// Global terrain render-chunk configuration seam (writes the path-selector globals at
    /// 0x00D1D058/0x00D1D06C before any tile loads). __cdecl, caller-cleaned.
    constexpr uintptr_t kInitRenderChunkSystem             = 0x007BA340;
    /// Terrain-wide shutdown seam, paired with the init above. __cdecl, caller-cleaned.
    constexpr uintptr_t kDestroyRenderChunkSystem          = 0x007BA5A0;
    /// The per-frame terrain vertex/index pool maintenance -- the place to enlarge or instrument the
    /// terrain buffer budget. __cdecl, caller-cleaned.
    constexpr uintptr_t kUpdateRenderChunkPools            = 0x007BA600;

    // Tile-area lifecycle, pooling and the per-frame terrain update lists
    /// The global "drop every chunk's clutter instance" sweep that runs when the detail-doodad pools
    /// are rebuilt -- the invalidation seam for custom clutter. __cdecl, caller-cleaned.
    constexpr uintptr_t kClearChunkDetailDoodads           = 0x0079E730;
    /// The global terrain GPU-buffer invalidation sweep (device reset / pool rebuild) -- the hook to
    /// also drop extension-owned terrain buffers. __cdecl, caller-cleaned.
    constexpr uintptr_t kClearChunkBufs                    = 0x0079E780;
    /// The per-tile streaming update that decides which chunks get prepared or purged this frame, i.e.
    /// the terrain LOD/residency driver. __cdecl, caller-cleaned.
    constexpr uintptr_t kUpdateTileArea                    = 0x007B4DF0;
    /// The per-frame drain of the pending-liquid-update list -- a detour can queue or veto liquid
    /// instance updates. __cdecl, caller-cleaned.
    constexpr uintptr_t kProcessChunkLiquidUpdates         = 0x007B5420;
    /// The per-frame drain of pending ground-effect updates, the correct place to batch custom clutter
    /// work. __cdecl, caller-cleaned.
    constexpr uintptr_t kProcessDetailDoodadUpdates        = 0x007B54A0;
    /// The per-frame drain of pending render-chunk (layer/buffer) updates -- hook to throttle or
    /// prioritize terrain texture rebuilds. __cdecl, caller-cleaned.
    constexpr uintptr_t kProcessRenderChunkUpdates         = 0x007B5500;
    /// Pairs with the chunk allocator; the last point a chunk pointer is valid before it returns to the
    /// pool free list. __cdecl, caller-cleaned.
    constexpr uintptr_t kFreeChunk                         = 0x007C0180;
    /// Releases a chunk's liquid instances; the symmetric teardown for anything keyed to a liquid
    /// instance. __cdecl, caller-cleaned.
    constexpr uintptr_t kFreeChunkLiquid                   = 0x007C04A0;
    /// The constructor behind the already-known low-detail tile allocator -- the place to widen or pre-
    /// seed a WDL tile object. __thiscall, caller-cleaned.
    constexpr uintptr_t kConstructAreaLow                  = 0x007C06E0;
    /// The pool allocator for chunk objects -- a detour can over-allocate to carry extension fields
    /// past the stock object size. __cdecl, caller-cleaned.
    constexpr uintptr_t kAllocChunk                        = 0x007C0830;
    /// The liquid-instance pool allocator, and the only place both the MCLQ and MH2O branches converge
    /// on allocation. __cdecl, caller-cleaned.
    constexpr uintptr_t kAllocChunkLiquid                  = 0x007C0980;
    /// The tile teardown path that retires the in-flight async read at area+0x70 -- essential when an
    /// extension serves tile data asynchronously. __thiscall, caller-cleaned.
    constexpr uintptr_t kCancelTileAreaLoad                = 0x007C35F0;
    /// Bulk chunk teardown for a tile (distance-driven), so an extension can release per-tile resources
    /// in one call instead of per chunk. __thiscall, 1 stack arg.
    constexpr uintptr_t kPurgeTileAreaChunks               = 0x007D6A90;
    /// The very first moment a tile-area object exists -- attach extension per-tile state before the
    /// filename or the file buffer are set. __thiscall, caller-cleaned.
    constexpr uintptr_t kConstructTileArea                 = 0x007D7050;
}
