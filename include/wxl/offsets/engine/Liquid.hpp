// Liquid material draw seam: the per-material water draw, its constant blocks, and the settings bank.
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

// INTERNAL to the core. The liquid render pass draws every visible liquid instance through one virtual
// per-material draw; this header records that draw for the water material, the constant blocks it
// uploads, and the per-liquid settings object that flows through it. A water renderer detours the
// draw to stash per-instance context and rides the deferred GxState cache to substitute its own SM3
// shader pair after the stock binds (see offsets/engine/Shader.hpp for the cache mechanics).
namespace wxl::offsets::engine::liquid
{
    // The water-material draw (specular variant). __thiscall on the material with 7 stack args, invoked
    // once per visible water instance by the liquid pass flush; `settings` is the per-liquid-type
    // settings object carrying every database column (colors/floats/ints/texture banks). ret 0x1C.
    constexpr uintptr_t kMaterialWaterRender = 0x008A5590;
    using MaterialWaterRenderFn = void(__fastcall*)(void* material, void* edx, void* env, void* geom,
                                                    void* anim, const float* camPos, const float* world,
                                                    const float* sphere, void* settings);
    // The no-specular sibling draw (vtable slot [2] of the no-spec water material class), used when
    // the video options disable water specular; same dispatch signature.
    constexpr uintptr_t kMaterialWaterNoSpecRender = 0x008A5900;

    // Geometry-provider GetBuffers (vtable slot [2] targets): called by the draw AFTER the stock
    // shader/state binds and immediately before the vertex/index streams are set and the batch is
    // issued -- the last seam where a deferred GxState slot write still wins the flush. Chunk = ADT
    // liquid batches, Mesh = WMO liquid. __thiscall, 4 stack args.
    constexpr uintptr_t kChunkGeomGetBuffers = 0x007D4AB0;
    constexpr uintptr_t kMeshGeomGetBuffers  = 0x007D43F0;
    using GeomGetBuffersFn = int(__fastcall*)(void* geom, void* edx, int fmt, void** vb, void** ib,
                                              void* batchDesc);

    // THE CHUNK GEOMETRY CACHE, and the reason a liquid draw does not always rebuild anything.
    // The provider keeps a buffer pair AND a copy of the batch descriptor on the factory itself. On
    // a call where the cached pair is live and the dirty flag is clear it returns both immediately
    // and the vertex emitter never runs -- which is most draws. So geometry substituted only on the
    // rebuild is geometry the draw sees once and then never again: the cached descriptor has to be
    // restated too, or every later draw of that chunk group describes the new vertices with the old
    // counts.
    constexpr size_t kGeomDirty      = 0x08;  // non-zero forces a full rebuild
    constexpr size_t kGeomChunkBuf   = 0x1C;  // buffered chunk geometry pointer, null until first built
    constexpr size_t kGeomBatchCache = 0x20;  // the four descriptor dwords, copied out on the fast path

    // The cache entry. Its two pools are created for it alone (see the Gx section below), sized to
    // the counts handed to Alloc -- which is why a denser lattice has to be reserved at that call
    // and cannot simply be written into a buffer built for the stock one.
    constexpr size_t kChunkBufVertex = 0x10;  // vertex buffer view*
    constexpr size_t kChunkBufIndex  = 0x14;  // index buffer view*
    constexpr uintptr_t kMapChunkBufAlloc = 0x007CF140;
    using MapChunkBufAllocFn = void*(__cdecl*)(int vertexCount, int indexCount, uint32_t tag,
                                               uint32_t vertexFormat);

    // Constant-upload choke: uploads the liquid VS block c0..c45 and PS block c0..c5 for every liquid
    // draw. void __cdecl(void); reads the two fixed blocks below.
    constexpr uintptr_t kConstantUpload = 0x008A3DA0;
    using ConstantUploadFn = void(__cdecl*)();

    // Material bank resolver: liquid type id -> cached per-MaterialID material. Material ids without a
    // stock factory (the modern families) return null -- the instance then never draws. Bounds-checked,
    // so extended LiquidMaterial rows are safe; a detour maps the null onto a stock family instead.
    constexpr uintptr_t kMaterialBankGetMaterial = 0x008A1FA0;
    using MaterialBankGetMaterialFn = void*(__cdecl*)(uint32_t liquidTypeId);

    // The fixed constant blocks the choke uploads. VS layout (proved by the shipped water vertex
    // shader): c0..c3 projection rows, c4 fog factors, c5..c8 modelview rows, c9/c10+c12 and
    // c13/c14+c16 the two texture matrices, c33 sun dir (view space), c34 ambient, c35 diffuse,
    // c36 specular color + exponent (.w), c37+ point-light data. PS: c4 fog color, c5 camera pos.
    constexpr uintptr_t kVsConstBlock  = 0x00D44CA8; // 46 x float4
    constexpr uintptr_t kPsConstBlock  = 0x00B24120; // 6 x float4
    constexpr unsigned  kVsBlockCount  = 46;         // stock uploads c0..c45; own constants go above
    constexpr unsigned  kPsBlockCount  = 6;          // stock uploads c0..c5; own constants go above

    // Per-liquid-type settings object (bank-cached, one per type id). Fetches the animated texture of
    // one slot: frame index from the cycle time, converted to the live GxTex. Returns null while the
    // slot's frames are still streaming (callers skip the draw or the slot that frame).
    constexpr uintptr_t kSettingsAnimTexture = 0x008A1D60;
    using SettingsAnimTextureFn = void*(__fastcall*)(void* settings, void* edx, int slot, uint32_t cycleMs);

    // Settings object fields (database columns resident per liquid type).
    constexpr size_t kSettingsColor0 = 0x300; // u32 BGRA
    constexpr size_t kSettingsColor1 = 0x304; // u32 BGRA
    constexpr size_t kSettingsInts   = 0x308; // int[4]
    constexpr size_t kSettingsFloats = 0x318; // float[18]

    // Settings bank cache: one settings pointer per liquid type id (settings = rows[id]); a linear
    // scan inverts a settings pointer back to its id, which the settings object itself does not carry.
    constexpr uintptr_t kSettingsBankRows  = 0x00D43B1C; // settings-object pointer array (indexed by type id)
    constexpr uintptr_t kSettingsBankCount = 0x00D43B18; // u32 array length

    // Per-chunk vertex emitter: fills one chunk's vertices into the batch VB through per-component
    // write cursors (each advances by the vertex stride when non-null). __thiscall on the
    // chunk-liquid geometry object; 7 stack args. Positions are written already batch-transformed -- world
    // coordinates for map liquids -- and uv0.y receives the depth-ramp value, which is where the
    // true-depth enrichment rewrites placements shipped without a depth stream.
    constexpr uintptr_t kChunkVertexEmit = 0x007CE390;
    using ChunkVertexEmitFn = void(__fastcall*)(void* chunk, void* edx, const void* mtx, int stride,
                                                uint8_t** pos, uint8_t** nrm, uint8_t** col,
                                                uint8_t** uv0, uint8_t** uv1);
    constexpr size_t kChunkLvf = 0x08; // liquid vertex format on the chunk (1 = stored u16 UVs)
    // Grid extent of the emitted lattice, as inclusive bounds. Vertices are emitted rows-outer,
    // columns-inner, so a vertex's index is row * cols + col -- which is what makes the lattice
    // subdividable: it is a regular grid, not a soup.
    constexpr size_t kChunkRow0 = 0x34;
    constexpr size_t kChunkCol0 = 0x38;
    constexpr size_t kChunkRow1 = 0x3C;
    constexpr size_t kChunkCol1 = 0x40;

    // ---- Gx geometry buffers ---------------------------------------------------------------
    // There is no standalone buffer object in this engine. A geometry pool owns one real D3D vertex or
    // index buffer of a fixed size; a buffer view is a 32-byte VIEW into a pool -- offset, stride, count
    // -- and several views can share one pool.
    //
    // The liquid chunk cache does NOT share. It creates a pool per cache entry, sized to that
    // entry's exact vertex and index counts, with usage 1 -- so its two streams are private, and
    // the only limit on writing into them is that pool's own byte size.
    constexpr uintptr_t kGxPoolCreate = 0x006876D0; // __thiscall, ret 0x14
    constexpr uintptr_t kGxBufCreate  = 0x00687660; // ecx unused, ret 0x10
    using GxPoolCreateFn = void*(__fastcall*)(void* dev, void* edx, uint32_t kind, uint32_t usage,
                                              uint32_t byteSize, uint32_t tag, const char* name);
    using GxBufCreateFn  = void*(__fastcall*)(void* dev, void* edx, void* pool, uint32_t itemSize,
                                              uint32_t itemCount, uint32_t byteOffset);
    constexpr uint32_t kGxPoolVertex  = 0;  // pool kind
    constexpr uint32_t kGxPoolIndex   = 1;
    /// Private and locked in place: the view's offset stays where it was created and every lock maps
    /// the same region. This is what the liquid chunk cache uses.
    constexpr uint32_t kGxUsageDedicated = 1;
    /// Dynamic, sub-allocating: the engine advances the view's offset on every lock and discards on
    /// wrap, which is the contract for geometry rewritten every frame by many unrelated callers.
    constexpr uint32_t kGxUsageStream = 2;

    // Buffer view fields. +0x1C is ours to set once the contents are written; +0x1D is the engine's
    // statement that the GPU side is healthy, and both must be true for a draw to be worth issuing.
    // +0x14 is also the byte count a lock maps, so it must be set before the lock, not after.
    constexpr size_t kBufPool   = 0x08;
    constexpr size_t kBufStride = 0x0C;
    constexpr size_t kBufCount  = 0x10;
    constexpr size_t kBufBytes  = 0x14;
    constexpr size_t kBufOffset = 0x18;
    constexpr size_t kBufValid  = 0x1C;
    constexpr size_t kBufGpuOk  = 0x1D;
    /// The pool's capacity in bytes, and the ceiling on anything written through a view onto it.
    constexpr size_t kPoolByteSize = 0x10;

    // Sets a view's stride and count (and clears its valid flag). Not virtual.
    constexpr uintptr_t kGxBufSizeSet = 0x006831A0;
    using GxBufSizeSetFn = void(__stdcall*)(void* buf, uint32_t stride, uint32_t count);

    // Device vtable slots. The base lock method returns nothing useful -- the D3D override is what
    // hands back the mapped pointer -- so these must go through the vtable, never the base address.
    constexpr unsigned kGxVtLock   = 0xD8 / 4;
    constexpr unsigned kGxVtUnlock = 0xDC / 4;
    using GxBufLockFn   = void*(__fastcall*)(void* dev, void* edx, void* buf);
    using GxBufUnlockFn = int(__fastcall*)(void* dev, void* edx, void* buf, int unused);

    // Vertex format 6, the liquid lattice: 44 bytes, position / normal / colour / two coordinate
    // sets. The stride a draw actually uses comes from the engine's format table, not from the
    // view, so a view of this format must be created with exactly this stride.
    constexpr uint32_t kVertexFormatLiquid = 6;
    constexpr uint32_t kVertexSizeLiquid   = 44;

    // Primitive type the sibling liquid provider (the WMO mesh one) writes for its own lattice.
    constexpr uint32_t kPrimTriangleList = 4;

    // The batch descriptor GetBuffers fills and the draw consumes, four dwords wide.
    struct BatchDesc
    {
        uint32_t primType;    // 4 for the liquid lattice
        uint32_t startIndex;
        uint32_t indexCount;
        uint32_t vertexRange; // low half min vertex index, high half max
    };

    // Terrain height of one map cell: bool __cdecl(mapChunk, const float* worldPos, int cellFromY,
    // int cellFromX, float* outHeight). The chunk/cell resolution from a world position mirrors the
    // client's walkable-height wrapper: grid = -(world - 17066.666) * 0.24, cell = round(g - 0.5),
    // area = grid[(cellX>>7 & 0x3f) * 0x40 + (cellY>>7 & 0x3f)] (skip when the interior flag or the
    // area's +0x70 marker is set), chunk = area[+0xBC + ((cellX>>3 & 0xF) * 0x10 + (cellY>>3 & 0xF)) * 4].
    constexpr uintptr_t kMapGetHeightTerrain = 0x007AD3B0;
    using MapGetHeightTerrainFn = int(__cdecl*)(void* mapChunk, const float* pos, int cellFromY,
                                                int cellFromX, float* outHeight);
    constexpr uintptr_t kMapAreaTable = 0x00CE48D0; // 64x64 area-chunk pointer grid
    constexpr uintptr_t kMapBDungeon  = 0x00CF08F4; // interior map flag: no terrain grid resident

    // LiquidType row fields consumed at draw time (row via the id-index in offsets/game/ADT.hpp).
    // The four darken columns are resident in every loaded row but unread by the stock draw.
    constexpr size_t kRowMaxDarkenDepth     = 0x18; // float, world units
    constexpr size_t kRowFogDarkenIntensity = 0x1C; // float 0..1
    constexpr size_t kRowAmbDarkenIntensity = 0x20; // float 0..1
    constexpr size_t kRowDirDarkenIntensity = 0x24; // float 0..1

    // The material bank resolver (kMaterialBankGetMaterial) switches on this column to pick the family
    // constructor (1 water, 2 ooze/slime, 3 magma) and has no default case: any other value falls
    // through every branch, caches a NULL material pointer for that row, and the next instance built
    // from it null-derefs its material pointer at draw time in the per-frame liquid render pass. A row id
    // GetMaterial can't find at all already self-heals ("Material Bank: Liquid type [%d] not found,
    // defaulting to water!", retried with id 1) -- classic content only ever shipped 1/2/3 here, so
    // there was never a reason to extend that same self-heal to a row that resolves but carries an
    // unusable value. A served liquid catalogue wider than the classic one can produce exactly that.
    constexpr size_t kRowMaterialId = 0x38; // u32, valid range {1, 2, 3}

    // Day/night info block: the per-frame interpolated zone water colors the procedural depth
    // gradient bakes each frame (close = shallow, far = deep; a packed 0xAARRGGBB colour), and the four
    // gradient alpha-curve floats that follow them.
    constexpr uintptr_t kDayNightInfo      = 0x00D38B00;
    constexpr size_t    kDnOceanCloseColor = 0x10C;
    constexpr size_t    kDnOceanFarColor   = 0x110;
    constexpr size_t    kDnRiverCloseColor = 0x114;
    constexpr size_t    kDnRiverFarColor   = 0x118;
    // The interpolated shallow/deep alpha pair per family (the light-params water alphas), laid out
    // as two start floats (ocean, river) then two end floats.
    constexpr size_t    kDnOceanAlphaShallow = 0x140;
    constexpr size_t    kDnRiverAlphaShallow = 0x144;
    constexpr size_t    kDnOceanAlphaDeep    = 0x148;
    constexpr size_t    kDnRiverAlphaDeep    = 0x14C;
    // The 18 interpolated band colors (BGRA), band order: 0 direct, 1 ambient, 2..6 sky top ->
    // horizon, 7 fog, 8 shadow, 9 sun, 10..13 clouds, 14..17 the water pairs above.
    constexpr uintptr_t kDnBandColors      = 0x00D38BD4;
}
