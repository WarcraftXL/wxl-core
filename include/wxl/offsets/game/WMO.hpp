// Map-object engine entries (root/group load, material resolve, visibility) and runtime object fields.
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

// INTERNAL to the core. Map-object engine entries (root/group load, material resolve, visibility) and
// runtime object fields. Modules never include this; they use wxl::game / wxl::events.
namespace wxl::offsets::game::wmo
{
    // --- load (rewrite buffers before the native parse) ---
    // Root read-completion callback (root): fires once after the async read fills the root buffer and
    // before the chunk walker runs. It is only a shim: it clears the async handle and TAIL-JUMPS to
    // the root finalizer (kRootCreateData below), whose single caller it is.
    constexpr uintptr_t kRootComplete = 0x007D8050;
    constexpr size_t kOffRootAsyncHandle = 0x1DC;
    // Group reader (group): the join point of both the sync and async group-load paths, run before the
    // group sub-chunk walk. Also a shim around the real sub-chunk walkers below; it decodes the 0x44-byte
    // MOGP header inline, then hands `groupBuffer + kGroupWalkCursorOffset` to kGroupWalk.
    constexpr uintptr_t kGroupParse = 0x007D82E0;
    // 0x14-byte outer chunk header + 0x44-byte MOGP body prefix: the fixed offset kGroupParse always
    // hands to kGroupWalk as `cursor`, computable directly from groupBuffer without waiting for
    // kGroupParse to run -- what lets WmoAsync.cpp's background stage1 call WalkGroupModern itself.
    constexpr size_t kGroupWalkCursorOffset = 0x58;
    // The MOGP-header fields kGroupParse copies into the group object immediately BEFORE it calls
    // kGroupWalk, addressed the same buffer-relative way the cursor is and for the same reason: a walk
    // that runs ahead of kGroupParse (WmoAsync.cpp's background stage1) would otherwise read them off a
    // group object that still holds zeros, or a pooled slot's previous occupant. The bbox especially --
    // it is what the group's BSP container stores as its own root clip box, so a walk that seeds the BSP
    // from a not-yet-copied bbox produces a tree every collision query rejects at the first node.
    constexpr size_t kGroupBufFlags        = 0x1C; // u32   -> kOffGroupFlags
    constexpr size_t kGroupBufBbox         = 0x20; // 6 x f -> kOffGroupBbox
    constexpr size_t kGroupBufTransBatches = 0x3C; // u16   -> kOffGroupTransBatchCount

    // --- the actual chunk walkers (the code a native modern reader replaces) ---
    // ROOT chunk walker. Positional: it skips MVER blind (+0x0C) then consumes 17 chunks in a fixed
    // order, storing content pointer + a count derived from chunkSize/stride, NEVER comparing a FourCC
    // (the sole exception is the optional trailing MCVP, tested against the on-disk dword "PVCM").
    // No bounds check, no validation, no early return: a missing or extra chunk silently desynchronises
    // every pointer that follows. This is why a modern root cannot be handed to it.
    constexpr uintptr_t kRootWalk = 0x007D7470;
    using Wmo_RootWalkFn = void(__fastcall*)(void* root, void* edx);
    // GROUP mandatory sub-chunk walker (this=group, cursor on the stack; __thiscall, `ret 4`). Consumes
    // MOPY, MOVI, MOVT, MONR, MOTV, MOBA positionally, then tail-calls kGroupWalkOptional.
    constexpr uintptr_t kGroupWalk = 0x007D7F50;
    using Wmo_GroupWalkFn = void(__fastcall*)(void* group, void* edx, void* cursor);
    // GROUP optional sub-chunk walker. Each block is gated on a bit of the MOGP flags at group+0x30 and
    // consumed in flag order, again without ever comparing a tag.
    constexpr uintptr_t kGroupWalkOptional = 0x007D7C30;
    // Root finaliser: runs kRootWalk, then CreateMaterials, copies the MOHD scalars, and allocates one
    // group object per MOGI entry into the inline array at kOffGroupArray.
    constexpr uintptr_t kRootCreateData = 0x007D7EB0;
    // Group read-completion callback (group): the async-read mirror of kRootComplete -- fires once the
    // async read fills the group buffer, clears the group's async handle (group+0x194), and tail-jumps
    // to kGroupParse. Found by tracing the group async-read setup. Backgrounding kGroupWalk's CPU work
    // means hooking HERE, not at kGroupParse: by the
    // time kGroupParse runs, the native callback has already cleared group+0x194, so anything gating on
    // that field has to own the moment BEFORE this call, not after -- see wxl-wmo's WmoAsync.cpp.
    constexpr uintptr_t kGroupComplete = 0x007D8570;
    using Wmo_GroupCompleteFn = void(__cdecl*)(void* group);
    constexpr size_t kOffGroupAsyncHandle = 0x194;

    // Frees a root object (root on the stack, cdecl)
    constexpr uintptr_t kFreeMapObj = 0x007BFF70;
    using Wmo_FreeMapObjFn = void(__cdecl*)(void* root);
    // Frees a group object (group on the stack, cdecl): the group front door.
    constexpr uintptr_t kFreeMapObjGroup = 0x007C0030;
    using Wmo_FreeMapObjGroupFn = void(__cdecl*)(void* group);

    // Blocks until a root is fully loaded (this = root, thiscall, no stack args)
    constexpr uintptr_t kWaitLoad = 0x007AE1C0;
    using Wmo_WaitLoadFn = void(__fastcall*)(void* root, void* edx);
    // Blocks until one group is fully loaded (this = root, thiscall, one stack arg = group index)
    constexpr uintptr_t kWaitLoadGroup = 0x007AEAB0;
    using Wmo_WaitLoadGroupFn = void(__fastcall*)(void* root, void* edx, int groupIndex);
    // Per-frame material update (this = root, thiscall, no stack args):
    constexpr uintptr_t kUpdateMaterials = 0x007A8520;
    using Wmo_UpdateMaterialsFn = void(__fastcall*)(void* root, void* edx);
    // Zeroes MOMT[i]+0x38 / +0x3C for every material -- the two GPU texture handles live INSIDE the
    // root file buffer, in the last 8 bytes of each 0x40-byte MOMT record.
    constexpr uintptr_t kCreateMaterials = 0x007D72D0;
    // BSP hand-off (this = group + kOffGroupBsp). Pure field copy, no allocation.
    // (mobnContent, nodeCount, mobrContent, refCount, &groupBbox).
    constexpr uintptr_t kBspInit = 0x0079ADC0;
    using Wmo_BspInitFn = void(__fastcall*)(void* bsp, void* edx, void* mobn, uint32_t nodeCount,
                                           void* mobr, uint32_t refCount, float* bbox);
    // Rewrites every MOCV BGRA byte in place. Run by the optional walker unless MOHD.flags & 0x8.
    constexpr uintptr_t kFixColorVertexAlpha = 0x007D7380;
    using Wmo_FixColorVertexAlphaFn = void(__fastcall*)(void* group, void* edx);

    // --- MOMT material record (stride 0x40, based at kOffMaterialBase, INSIDE the root file buffer) ---
    // texture_1 / texture_2 are byte offsets into MOTX on the client, and texture FileDataIDs on a modern
    // root (which ships no MOTX at all). The last 8 bytes are NOT file data: the client stores the two
    // live GPU texture handles there, which is why CreateMaterials zeroes them at load.
    constexpr size_t kMomtStride       = 0x40;
    constexpr size_t kOffMomtFlags     = 0x00; // u32; read at material+0 by both batch draws (0x007A9380 /
                                               // 0x007AC6A0) to fork the per-batch lighting mode
    // Bit 0x1 = unlit material: both draws pass `~flags & 1` (or the trans/exterior-segment variant) to
    // the per-batch light setter, so an unlit batch renders with lighting OFF (vertex colour multiplies
    // the texture) while a lit batch renders from the scene/WMO ambient light. The modern combine keys
    // the SAME bit: unlit multiplies 2x vertex colour, lit ADDS 2x vertex colour to its ambient.
    constexpr uint32_t kMomtFlagUnlit  = 0x1;
    // Bits 0x40 / 0x80 = clamp S / clamp T: both batch draws bind stage 0 with wrap = the INVERSE of
    // these bits (`~(flags >> 6) & 1`, `~(flags >> 7) & 1`), so a clear bit means repeat.
    constexpr uint32_t kMomtFlagClampS = 0x40;
    constexpr uint32_t kMomtFlagClampT = 0x80;
    constexpr size_t kOffMomtShader    = 0x04; // u32; CreateMaterial rewrites 3/5/6 -> 4 when tex2 is empty
    // Highest shader id this client has an effect for. The lookup behind it is UNCHECKED: a higher id
    // selects past the effect table, the shader-effect binder stores a null current effect, and the
    // next shader bind then dereferences near address 0.
    constexpr uint32_t kMaxClientShaderId = 6;
    constexpr size_t kOffMomtBlend     = 0x08; // u32
    constexpr size_t kOffMomtTexture1  = 0x0C; // u32 MOTX offset | modern FileDataID
    constexpr size_t kOffMomtTexture2  = 0x18; // u32 MOTX offset | modern FileDataID
    constexpr size_t kOffMomtDiffColor = 0x1C; // u32 BGRA diffuse tint colour
    constexpr size_t kOffMomtHandle1   = 0x38; // runtime texture handle (0 = not loaded yet)
    constexpr size_t kOffMomtHandle2   = 0x3C; // runtime texture handle
    // Modern shader 23: a four-layer height-blended material. Its MOMT record repurposes the fields past
    // texture_2 as SEVEN extra texture FileDataIDs: diffuse layers A..D at +0x0C/+0x18/+0x24/+0x28, an
    // environment map at +0x2C, and height maps A..D at +0x30..+0x3C. The last two height slots ALIAS the
    // runtime handle fields above, which material creation overwrites -- a reader must copy all nine ids
    // out of the record BEFORE the client's material path runs (i.e. during the root walk).
    constexpr uint32_t kShaderIdLayered = 23;
    constexpr size_t kOffMomtLayerDiffuse[4] = { 0x0C, 0x18, 0x24, 0x28 };
    constexpr size_t kOffMomtLayerEnv        = 0x2C;
    constexpr size_t kOffMomtLayerHeight[4]  = { 0x30, 0x34, 0x38, 0x3C };
    // Name CreateMaterial substitutes for an empty texture_1, and the global that disables the second
    // texture entirely when the shader pipeline is off.
    constexpr const char kFallbackTextureName[] = "createcrappygreentexture.blp";
    constexpr uintptr_t kShaderEffectsEnabled = 0x00D43020; // global shader-effects-enabled flag (u32)

    // --- MOBA render batch (stride 0x18, based at group+0x0F8, INSIDE the group file buffer) ---
    // The client's own record. A modern file keeps every field EXCEPT the two below.
    constexpr size_t kMobaStride        = 0x18;
    constexpr size_t kOffMobaBbox       = 0x00; // 6 x i16 (min xyz, max xyz), group-local
    constexpr size_t kOffMobaStartIndex = 0x0C; // u32 into MOVI
    constexpr size_t kOffMobaCount      = 0x10; // u16 index count
    constexpr size_t kOffMobaMinIndex   = 0x12; // u16 first vertex index
    constexpr size_t kOffMobaMaxIndex   = 0x14; // u16 last vertex index
    constexpr size_t kOffMobaFlags      = 0x16; // u8; the client uses the HIGH nibble as frame scratch
    constexpr size_t kOffMobaMaterial   = 0x17; // u8 material index -- read at NINE sites, six in render
    // Modern delta: the material index moved to a u16 here, and the flag bit that announces it sits in
    // the low nibble of +0x16, which the client masks around (`and ..., 0x0F`) but never reads.
    constexpr size_t   kOffMobaMaterialModern = 0x0A;
    constexpr uint8_t  kMobaFlagMaterialModern = 0x02;
    // Per-batch AABB cull. __cdecl(record), returns non-zero when the batch is culled; ExtRender and
    // IntRender both gate their batch on it. It reads the six i16 above -- which on a modern record are
    // all zero except max.z, where the material index lives -- so every modern batch claims a
    // degenerate box at the group origin.
    constexpr uintptr_t kCullBatch = 0x007A7630;
    using Wmo_CullBatchFn = char(__cdecl*)(void* mobaRecord);

    // WMO batch-draw leaves (source of the composite-shader draw seam documented in CompositeShader.hpp).
    // Both are __thiscall(this=root, group, int flag) with a `ret 8` epilogue, invoked through a vtable
    // (no direct E8 caller). Each iterates the group's MOBA batches and, per batch, activates the
    // material's effect collection + binds VS/PS (kEffectBind) then issues an indexed draw that flushes
    // GxState. When root->MOHD.flags & 0x2 (the modern "unified render path" bit -- set on every modern
    // file observed) they tail-call the alternate
    // renderer, so hooking these two entries brackets ALL modern WMO batch rendering, including that
    // delegated path. Used only to stash the current root's modern verdict around the batch loop.
    constexpr uintptr_t kExtRender = 0x007AC6A0; // single exterior-batch loop
    constexpr uintptr_t kIntRender = 0x007AC9F0; // trans + interior + exterior segments
    using Wmo_RenderLeafFn = void(__fastcall*)(void* root, void* edx, void* group, int flag);

    // Group two-UV format flag. Set once at group finalize (0x007D8561 `or [group+0x198],8`) iff ANY
    // batch uses a shader-6 (Composite) material -- a whole-group decision. When set, the group's vertex
    // buffer is built in the two-UV format (stride 0x30): UV set0 at vertex offset 0x20, set1 at 0x28,
    // so BOTH UV sets are present on every vertex and reachable by the per-batch vertex shader. That is
    // the precondition for routing a single-layer batch onto set1 (features/wmonative/CompositeShader).
    constexpr size_t   kOffGroupFormatFlags = 0x198;
    constexpr uint32_t kGroupFlagTwoUv      = 0x8;

    // --- MOHD (root+kOffMohd content) ---
    constexpr size_t kOffMohdFlags = 0x3C; // u32; bit 0x1 / 0x4 / 0x8 gate group-side behaviour
    constexpr uint32_t kMohdFlagSkipColorFix = 0x8; // set => the MOCV alpha rewrite is skipped
    // MOGI entry flag bit the root walker clears in place when the root carries no skybox name.
    constexpr uint32_t kMogiFlagShowSky = 0x00040000;
    // MOPT plane repair constants used when the stored plane distance is NaN.
    constexpr float kPortalPlaneRepairDistance = 800000.0f;

    // --- instance placement (per-instance scale) ---
    // Spawns a placed WMO instance from one MODF record and builds its world transform. __cdecl
    // (ctx, modf, tileOrigin, dedup); arg2 is the 0x40-byte MODF record, the return is the instance.
    // When dedup is set and the uniqueId is already loaded it returns the existing instance instead.
    constexpr uintptr_t kSpawnFromModf = 0x007BF460;
    using Wmo_SpawnFromModfFn = void*(__cdecl*)(void* ctx, void* modf, const float* tileOrigin, int dedup);
    // MODF record: u16 per-instance scale at +0x3E (factor = value/1024; 0 and 1024 both mean 1.0). The
    // Client treats it as padding and renders every WMO at 1.0.
    constexpr size_t kOffModfScale = 0x3E;
    // Instance transform matrices (4x4 row-major floats, translation in row 3): +0x70 the model-to-world
    // placement, +0xB0 its AFFINE INVERSE (built by kSpawnFromModf as AffineInverse of +0x70, translation
    // row included) and the matrix every collision sweep, portal walk and local-bounds test uses to reach
    // model space. Resizing an instance therefore means scaling the 3x3 rows of +0x70 by s and BOTH the
    // 3x3 and the translation row of +0xB0 by 1/s; a fresh instance's basis is orthonormal, which is what
    // makes |row0| == 1 a usable "not yet scaled" test.
    constexpr size_t kOffInstanceRenderMatrix    = 0x70;
    constexpr size_t kOffInstanceCollisionMatrix = 0xB0;

    // --- material / visibility guards ---
    // Material/texture resolver (model, materialIndex): computes the material entry and resolves its
    // texture-name offsets. It does not bounds-check materialIndex.
    constexpr uintptr_t kResolveMaterialTexture = 0x007D7710;
    // Portal-visibility traversal (model, groupIndex, a, b, out): portal-driven group visibility. It
    // assumes every referenced group object exists.
    constexpr uintptr_t kPortalVisibility = 0x007AF520;
    // Group-resident accessor (model, groupIndex, force): the join point of the resident-group queries.
    // It does not bounds-check groupIndex.
    constexpr uintptr_t kGroupResidentAccessor = 0x007AEA80;

    /**
     * @brief Chunk tag as the client compares it: the four on-disk bytes read back as one LE dword.
     *
     * WMO stores tags REVERSED on disk, so `MOHD` is the byte sequence `44 48 4F 4D` and reads back as
     * `0x4D4F4844`. That is exactly what this builds, and it matches the single real comparison the
     * stock walker makes (`cmp dword ptr [esi], 0x4D435650` for MCVP at 0x007D76DB).
     */
    constexpr uint32_t WmoTag(const char (&s)[5])
    {
        return (static_cast<uint32_t>(static_cast<uint8_t>(s[0])) << 24) |
               (static_cast<uint32_t>(static_cast<uint8_t>(s[1])) << 16) |
               (static_cast<uint32_t>(static_cast<uint8_t>(s[2])) << 8)  |
                static_cast<uint32_t>(static_cast<uint8_t>(s[3]));
    }

    /**
     * @brief One chunk slot as the stock walkers fill it: where the content pointer goes, where the
     *        element count goes, and the stride the count is derived with.
     *
     * The client never reads a count field out of the file: every count is `chunkSize / stride`.
     * `stride == 1` marks the three string blobs (MOTX, MOGN, MODN) whose "count" is the raw byte
     * size. `countField == 0` means the slot stores a pointer only.
     */
    struct ChunkSlot
    {
        uint32_t tag;        ///< FourCC in memory order ('MOHD'), not the reversed on-disk dword
        size_t   ptrField;   ///< object offset receiving the chunk CONTENT pointer
        size_t   countField; ///< object offset receiving the derived count (0 = none)
        uint32_t stride;     ///< element size; 1 = the count is the raw byte size
    };

    /// ROOT slots, exactly as `kRootWalk` fills them. Order here is the canonical 335 walk order,
    /// which a tag-driven walker does not depend on -- it is kept so the two can be diffed.
    constexpr ChunkSlot kRootSlots[] = {
        { WmoTag("MOHD"), 0x120, 0,     0    },
        { WmoTag("MOTX"), 0x124, 0x164, 1    }, // byte size
        { WmoTag("MOMT"), 0x160, 0x19C, 0x40 },
        { WmoTag("MOGN"), 0x128, 0x168, 1    }, // byte size
        { WmoTag("MOGI"), 0x130, 0x16C, 0x20 }, // count doubles as the group count
        { WmoTag("MOSB"), 0x12C, 0,     0    },
        { WmoTag("MOPV"), 0x134, 0x170, 0x0C },
        { WmoTag("MOPT"), 0x138, 0x174, 0x14 },
        { WmoTag("MOPR"), 0x13C, 0x178, 0x08 },
        { WmoTag("MOVV"), 0x140, 0x17C, 0x0C },
        { WmoTag("MOVB"), 0x144, 0x180, 0x04 },
        { WmoTag("MOLT"), 0x148, 0x184, 0x30 },
        { WmoTag("MODS"), 0x14C, 0x188, 0x20 },
        { WmoTag("MODN"), 0x150, 0x18C, 1    }, // byte size
        { WmoTag("MODD"), 0x154, 0x190, 0x28 },
        { WmoTag("MFOG"), 0x158, 0x194, 0x30 },
        { WmoTag("MCVP"), 0x15C, 0x198, 0x10 }, // optional; the only tag the stock walker actually compares
    };

    /// GROUP sub-chunk slots, merging the mandatory walker (kGroupWalk) and the optional one
    /// (kGroupWalkOptional). MOTV and MOCV legitimately appear TWICE in a group: the second occurrence
    /// feeds the "2nd set" slot, so a tag-driven walker must count occurrences rather than assume one.
    /// MOBN/MOBR are absent here: they are not stored as slots but handed to kBspInit (see the walker).
    constexpr ChunkSlot kGroupSlots[] = {
        { WmoTag("MOPY"), 0x0DC, 0x150, 0x02 }, // faces
        { WmoTag("MOVI"), 0x0E0, 0x154, 0x02 }, // INDICES (3 per face), not faces -- see kOffGroupMoviCount
        { WmoTag("MOVT"), 0x0E8, 0x15C, 0x0C },
        { WmoTag("MONR"), 0x0EC, 0x160, 0x0C },
        { WmoTag("MOTV"), 0x0F0, 0x164, 0x08 },
        { WmoTag("MOBA"), 0x0F8, 0x16C, 0x18 },
        { WmoTag("MOLR"), 0x100, 0x170, 0x02 },
        { WmoTag("MODR"), 0x104, 0x174, 0x02 },
        { WmoTag("MOCV"), 0x108, 0x178, 0x04 },
        { WmoTag("MORI"), 0x0E4, 0x158, 0x02 },
    };
    /// Second-occurrence destinations for the two sub-chunks a group may carry twice.
    constexpr ChunkSlot kGroupSlotMotv2 = { WmoTag("MOTV"), 0x0F4, 0x168, 0x08 };
    constexpr ChunkSlot kGroupSlotMocv2 = { WmoTag("MOCV"), 0x10C, 0x17C, 0x04 };
    /// MORB stores a content pointer with no count (the stock walker derives nothing for it).
    constexpr size_t kOffGroupMorb = 0x0FC;

    // --- root object fields ---
    constexpr size_t kOffMohd         = 0x120; // MOHD content pointer (see kOffMohdFlags)
    constexpr size_t kOffRootBuffer    = 0x1CC; // root buffer pointer
    constexpr size_t kOffRootSize      = 0x1D0; // root buffer byte size
    constexpr size_t kOffNameInline    = 0x1C;  // inline path string
    constexpr size_t kOffMaterialBase  = 0x160; // material-record base pointer
    constexpr size_t kOffMaterialCount = 0x19C; // material count
    constexpr size_t kOffGroupArray    = 0x1F8; // group runtime-object array (stride 4)
    constexpr size_t kOffGroupCount    = 0x1F4; // group count (the group-array bound)
    // Per-group bbox table on the root.
    constexpr size_t kOffMogiTable     = 0x130; // group-info table base pointer
    constexpr size_t kOffMogiCount     = 0x16C; // group-info entry count (kRootSlots[4].countField)
    constexpr size_t kMogiStride       = 0x20;  // group-info entry stride
    constexpr size_t kOffMogiBbox      = 0x04;  // bbox min within an entry (max at +0x10)
    // Standalone names for the handful of kRootSlots entries a call site reads back by itself, once the
    // generic root walk has already filled them (see kRootSlots for the full 17-slot table this aliases).
    constexpr size_t kOffMosb      = 0x12C; // MOSB content pointer (skybox name; empty = no skybox)
    constexpr size_t kOffMopt      = 0x138; // MOPT content pointer (portal planes)
    constexpr size_t kOffMoptCount = 0x174; // MOPT entry count
    constexpr size_t kOffModn      = 0x150; // MODN content pointer (doodad name blob)
    constexpr size_t kOffRootModd  = 0x154; // MODD content pointer (doodad placements)

    // --- group object fields ---
    constexpr size_t kOffGroupBuffer = 0x184; // group buffer pointer
    constexpr size_t kOffGroupSize   = 0x188; // group buffer byte size
    constexpr size_t kOffGroupRoot   = 0x18C; // -> parent root object
    // Standalone names for the handful of kGroupSlots entries a call site reads back by itself.
    constexpr size_t kOffGroupMotv        = 0x0F0; // MOTV content pointer (base UV set)
    constexpr size_t kOffGroupMonr        = 0x0EC; // MONR content pointer (vertex normals)
    constexpr size_t kOffGroupVertexCount = 0x15C; // MOVT entry count (kGroupSlots[2].countField)
    // Group bbox (min xyz, max xyz), passed whole to kBspInit; kOffGroupBboxMinZ below is its 3rd float.
    constexpr size_t kOffGroupBbox = 0x34;

    // Resolved LiquidType.dbc id, filled during group finalize: MOGP+0x48 verbatim for a modern-format
    // root (already a real LiquidType id -- modern WMOs write one directly, no legacy conversion needed),
    // or MOGP+0x48 remapped through the small 1..20 legacy family table otherwise. Either way, by the time
    // group finalize returns this field holds the id the liquid material lookup will be asked to resolve
    // -- same unchecked-MaterialID hazard as the ADT/MH2O path (see kLiquidTypeMaterialId in ADT.hpp).
    constexpr size_t kOffGroupLiquidType = 0x144;

    // --- visibility-probe entries and globals (cull path) ---
    constexpr uintptr_t kPortalRectAccum   = 0x007A8F20; // (portal, moprRef, portalState, exteriorFlag)
    constexpr uintptr_t kFrustumAabbTest   = 0x009839E0; // (frustum, bbox); 0=culled, 3=inside
    constexpr uintptr_t kHorizonAabbTest   = 0x0078FDC0; // (bbox, mode); 0=visible, 2=horizon-culled
    constexpr uintptr_t kCameraInGroupTest = 0x007AE880; // (root, camA, camB, groupIndex)
    constexpr uintptr_t kIndoorFlag        = 0x00CD87A4; // != 0 when camera is in an indoor group
    // Same global as kIndoorFlag, read as a pointer: the map-object instance the camera is currently
    // inside (null when outdoors). Its kOffInstanceRoot field points to the root that carries the path.
    constexpr uintptr_t kCurrentInteriorInstance = 0x00CD87A4;
    // Instance field: pointer to the owning root object (the one with the inline path at kOffNameInline).
    constexpr size_t kOffInstanceRoot = 0xF4;
    // Doodad-set selection on the placed instance: the primary selected set (from MODF+0x3A) and up to 3
    // extra sets. A doodad renders iff its owning set is 0, the selected set, or one of the extra sets.
    constexpr size_t kOffInstanceDoodadSet = 0x100; // u32 selected set index
    constexpr size_t kOffInstanceExtraSets = 0x150; // u16[3] extra set indices (0 = unused)
    // Root doodad-set table fields: the MODS array and its entry count.
    constexpr size_t kOffRootMods        = 0x14C; // -> MODS array (SMODoodadSet, stride kModsStride)
    constexpr size_t kOffRootDoodadSets  = 0x188; // u32 doodad-set count (MODS_size / 0x20)
    constexpr size_t kOffRootDoodadDefs  = 0x190; // u32 doodad-def count (MODD bound)
    constexpr size_t kModsStride         = 0x20;  // SMODoodadSet stride
    constexpr size_t kOffModsStart       = 0x14;  // u32 first MODD index in the set
    constexpr size_t kOffModsCount       = 0x18;  // u32 MODD count in the set
    constexpr uintptr_t kPortalRect        = 0x00ADF58C; // float[5]: minX,minY,maxX,maxY,nearExtent
    constexpr uintptr_t kOutdoorEnabled    = 0x00ADF59C; // float; >= 0 when the outdoor pass runs
    // Values the OUTDOOR branch of the world-scene render writes into the five floats above: a full
    // 0..1 screen rect and a zero gate. Reproducing them is what "the exterior is fully visible" means
    // to this client -- the indoor branch instead seeds an EMPTY rect (+FLT_MAX / -FLT_MAX) and a gate
    // of -1, which portal traversal then unions into.
    constexpr float kPortalRectFullScreen[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
    constexpr float kOutdoorEnabledOn        = 0.0f;

    // Portal traversal for the interior instance, run once per frame immediately BEFORE the outdoor
    // gate is tested. __fastcall(this = the interior instance, stack arg = the traversal state), `ret 4`.
    // Call site 0x0079AA22: `mov ecx,[0x00CD87A4]` (the interior instance) then `push 0x00CDB0D4`.
    // The gate right after is a plain `fld [kOutdoorEnabled]` / `fcom` against 0.0, so raising the gate
    // in a post-hook makes the client take its OWN exterior path -- no code patch, nothing reimplemented.
    constexpr uintptr_t kPortalTraverse = 0x007B3B20;
    using Wmo_PortalTraverseFn = void(__fastcall*)(void* instance, void* edx, void* state);
    /// The traversal-state argument that identifies the pre-gate call site (the other one is 0x00CDB0E4).
    constexpr uintptr_t kPortalTraverseGateState = 0x00CDB0D4;

    // Builds CPU occluders from a group whose name is literally "antiportal", then zeroes the group's
    // interior/exterior batch counts so it never draws. __fastcall(this = group).
    // The client feeds those occluders to its ANGULAR clip buffer (384 slots) -- a coarse solid-angle test
    // with no depth. A modern hi-Z occlusion buffer instead rasterises the same meshes, so a large slab
    // only hides what is genuinely behind its silhouette. A modern antiportal (the bridge's is 173 x 48
    // units) therefore occludes far more on the client than the artist ever intended.
    constexpr uintptr_t kCreateOccluders = 0x007D81C0;
    using Wmo_CreateOccludersFn = void(__fastcall*)(void* group, void* edx);
    /// Group-flag bit modern content sets on an antiportal group; present in the files, unknown to the client.
    constexpr uint32_t kGroupFlagAntiportal = 0x04000000;
    /// Pool allocator for one occluder record. The record holds TWO C3Vectors at +0x04 and +0x10 --
    /// the two vertices of a triangle that sit ABOVE its mean Z, i.e. the top edge. Nothing else.
    constexpr uintptr_t kAllocOccluder = 0x007B0250;
    using Wmo_AllocOccluderFn = void*(__cdecl*)();
    constexpr size_t kOffOccluderVertices = 0x04;
    /// Projects one occluder's top edge and raises the horizon over the azimuths it spans.
    /// __fastcall(this = occluder + kOffOccluderVertices); tail-calls the generic edge projector with
    /// vertexCount = 2. Called once per occluder per frame, with the instance transform already live.
    constexpr uintptr_t kAddOccluderEdge = 0x007CC880;
    using Wmo_AddOccluderEdgeFn = void(__fastcall*)(void* vertices, void* edx);
    /// Horizon table: 384 floats, one per azimuth bucket (`round(x' * 64 - bias) + 0xC0`, x' being the
    /// perspective-divided screen abscissa). Each entry holds the highest occluded elevation y' seen so
    /// far. Reset to -1000000 every frame. kHorizonAabbTest culls a box iff its own maximum y' sits at
    /// or below this value in EVERY bucket it spans -- a skyline with no lower bound, which is why a
    /// floating slab hides the ground underneath it.
    constexpr uintptr_t kClipBuffer      = 0x00CD8938;
    constexpr size_t    kClipBufferSlots = 384;
    /// One byte per azimuth bucket, set while a map-object edge raises the horizon there. The terrain
    /// pass tests it before resetting a bucket, so a probe of the projector must restore it too.
    constexpr uintptr_t kClipBufferMask  = 0x00CD87B8;
    /// Group bbox minimum Z (MOGP header +0x14), i.e. the floor of the antiportal slab.
    constexpr size_t kOffGroupBboxMinZ = 0x3C;
    /// Batch counts CreateOccluders zeroes so the antiportal shell itself never renders.
    constexpr size_t kOffGroupIntBatchCount = 0x5E;
    constexpr size_t kOffGroupExtBatchCount = 0x60;

    // Near end of the distance band the exterior pass submits within: something is drawn only when
    // `kExteriorNearBand < dist < farClip - 33.33`. The TRUE outdoor branch sets it to -10000 (draw
    // everything); the interior-with-portal branch instead derives it as `kOutdoorEnabled + 33.33`,
    // because there the gate holds the portal's near extent. Raising the gate to 0 to re-enable the
    // exterior therefore leaves a 33-yard NEAR HOLE unless this is restored to the outdoor value.
    constexpr uintptr_t kExteriorNearBand      = 0x00CD8780;
    constexpr float     kExteriorNearBandOpen  = -10000.0f;
    // Culls and submits the world into the sort table. __cdecl(rect, restrictedToPortalRect).
    // Called immediately AFTER kExteriorNearBand is derived, which is the only seam where the band can
    // be corrected before it is used.
    constexpr uintptr_t kCullSortTable = 0x0079A790;
    using Wmo_CullSortTableFn = void(__cdecl*)(void* rect, int restricted);

    // Decides which map object / group the viewer is inside; writes the interior instance that lands in
    // kCurrentInteriorInstance. __cdecl(camA, camB, radius, &instanceOut, &groupOut).
    constexpr uintptr_t kLocateViewerMapObjs = 0x007D59B0;
    using Wmo_LocateViewerMapObjsFn = char(__cdecl*)(const float* camA, const float* camB, float radius,
                                                    void** instanceOut, void** groupOut);

    // --- group fields the outdoor rule reads ---
    constexpr size_t kOffGroupFlags           = 0x30; // MOGP flags, copied by kGroupParse
    constexpr size_t kOffGroupTransBatchCount = 0x5C; // u16, MOGP+0x28
    constexpr uint32_t kGroupFlagIndoor       = 0x2000;
    constexpr uint32_t kGroupFlagExterior     = 0x8;  // SMOGROUP_EXTERIOR: MOCV is a neutral placeholder, not baked light
    // Modern content re-enables the exterior from inside an interior group under three conditions the
    // client has no equivalent for. Measured across the sampled files: A and C never occur, B holds
    // for roughly a third of interior groups -- those are the groups where standing inside kills the
    // terrain on the client but not in the modern original.
    constexpr uint32_t kGroupFlagExteriorLit   = 0x40;       // condition A
    constexpr uint32_t kGroupFlagExteriorPortal = 0x20000000; // condition C

    // --- camera-in-group containment (cull path) ---
    constexpr uintptr_t kBspRaycastRefine = 0x007CB0C0;
    // Group collision fields (group object). MOVI = u16[3] indices per face; MOVT = C3Vector vertices.
    constexpr size_t kOffGroupBsp       = 0x64;  // BSP container (null when the group has no BSP)
    constexpr size_t kOffGroupMovi      = 0xE0;  // triangle vertex indices (u16[3] per face, stride 6)
    constexpr size_t kOffGroupMovt      = 0xE8;  // vertices (C3Vector, stride 0x0C)
    // MOVI holds one u16 index per triangle CORNER, so +0x154 counts INDICES (MOVI_size / 2), not faces.
    // The face count lives at +0x150 (MOPY_size / 2) -- proven by CreateOccluders, which walks faces with
    // the one and indices with the other. Reading +0x154 as a face count over-runs the array by 3x.
    constexpr size_t kOffGroupMoviCount = 0x154; // MOVI index count (MOVI_size / 2)
    constexpr size_t kOffGroupFaceCount = 0x150; // MOPY face count (MOPY_size / 2)
    // BSP container fields (relative to kOffGroupBsp).
    constexpr size_t kOffBspNodes     = 0x00; // node array pointer (0 when the group has no BSP)
    constexpr size_t kOffBspMobr      = 0x08; // collision face-index array (u16 into MOVI)
    constexpr size_t kOffBspMobrCount = 0x10; // collision face-index count
    constexpr size_t kOffBspBboxMax   = 0x58; // local bbox max (3 floats); min at +0x4C

    // --- signatures ---
    // Root read-completion (root on stack).
    using Wmo_RootCompleteFn = void(__cdecl*)(void* root);
    // Group reader: native this-in-ECX.
    using WmoGroup_ParseFn = void(__fastcall*)(void* group, void* edx);
    // Material/texture resolver: native this-in-ECX; declared with a dummy second parameter so the
    // trampoline keeps materialIndex on the stack.
    using Wmo_ResolveMaterialTextureFn = void(__fastcall*)(void* model, void* edx, int materialIndex);
    // Portal-visibility traversal: native this-in-ECX; declared with a dummy second parameter so the
    // trampoline keeps the trailing arguments on the stack.
    using Wmo_PortalVisibilityFn = unsigned int(__fastcall*)(void* model, void* edx, unsigned int groupIndex, float* a, float* b, unsigned int* out);
    // Group-resident accessor: native this-in-ECX; declared with a dummy second parameter so the
    // trampoline keeps the trailing arguments on the stack.
    using Wmo_GroupResidentFn = unsigned int(__fastcall*)(void* model, void* edx, unsigned int groupIndex, unsigned int force);
    // Visibility-probe signatures.
    using Wmo_PortalRectAccumFn = void(__fastcall*)(void* portal, void* edx, void* moprRef, void* portalState, int exteriorFlag);
    using Wmo_FrustumAabbTestFn = uint32_t(__fastcall*)(void* frustum, void* edx, void* bbox);
    using Wmo_HorizonAabbTestFn = uint32_t(__cdecl*)(void* bbox, uint32_t mode);
    using Wmo_CameraInGroupTestFn = uint32_t(__fastcall*)(void* root, void* edx, float* camA, float* camB, uint32_t groupIndex);
    using Wmo_BspRaycastRefineFn = char(__fastcall*)(void* group, void* edx, float* seg, float* distScale, unsigned int mask, void* a3, void* a4, void* instance);

    // --- typed views over the objects above ---
    // The constants are the curated landmarks; these structs give named, typed access to the same fields,
    // with every member offset checked against a constant at compile time (a wrong padding fails the build).
    // Only known fields are named; the gaps are explicit padding. Pointers are 4 bytes on the 32-bit client.
#pragma pack(push, 1)
    /**
     * @brief Map-object root: the parsed root object that owns the material table and the group array.
     *
     * Pointer-valued fields are stored as uint32_t, not void*, everywhere except the LAST field of a
     * struct: with more than one such field, sizeof(void*) would drive the padding between them, and
     * this header is 32/64-bit-neutral (sizeof(uint32_t) is not).
     */
    struct Root
    {
        uint8_t  _pad00[kOffNameInline];
        char     nameInline[kOffMogiTable - kOffNameInline]; // kOffNameInline (inline NUL-terminated path)
        void*    mogiTable;        // kOffMogiTable -> per-group bbox table (stride kMogiStride)
        uint8_t  _pad134[kOffMaterialBase - (kOffMogiTable + sizeof(void*))];
        void*    materialBase;     // kOffMaterialBase -> material-record base
        uint8_t  _pad164[kOffMaterialCount - (kOffMaterialBase + sizeof(void*))];
        uint32_t materialCount;    // kOffMaterialCount
        uint8_t  _pad1a0[kOffRootBuffer - (kOffMaterialCount + sizeof(uint32_t))];
        uint32_t rootBuffer;       // kOffRootBuffer -> root file buffer
        uint32_t rootSize;         // kOffRootSize (root buffer byte size)
        uint8_t  _pad1d4[kOffGroupCount - (kOffRootSize + sizeof(uint32_t))];
        uint32_t groupCount;       // kOffGroupCount (the group-array bound)
        void*    groupArray[1];    // kOffGroupArray (group runtime objects, stride 4)
    };
    static_assert(offsetof(Root, nameInline)    == kOffNameInline,    "Root.nameInline");
    static_assert(offsetof(Root, mogiTable)     == kOffMogiTable,     "Root.mogiTable");
    static_assert(offsetof(Root, materialBase)  == kOffMaterialBase,  "Root.materialBase");
    static_assert(offsetof(Root, materialCount) == kOffMaterialCount, "Root.materialCount");
    static_assert(offsetof(Root, rootBuffer)    == kOffRootBuffer,    "Root.rootBuffer");
    static_assert(offsetof(Root, rootSize)      == kOffRootSize,      "Root.rootSize");
    static_assert(offsetof(Root, groupCount)    == kOffGroupCount,    "Root.groupCount");
    static_assert(offsetof(Root, groupArray)    == kOffGroupArray,    "Root.groupArray");

    /**
     * @brief Map-object group: MOGP flags/bbox/batch-counts, resolved liquid type, file buffer, and the
     *        back pointer to the root.
     */
    struct Group
    {
        uint8_t  _pad00[kOffGroupFlags];
        uint32_t flags;             // kOffGroupFlags (MOGP flags, copied by kGroupParse)
        uint8_t  _pad34[kOffGroupBboxMinZ - (kOffGroupFlags + sizeof(uint32_t))];
        float    bboxMinZ;          // kOffGroupBboxMinZ (group bbox min Z, the antiportal floor)
        uint8_t  _pad40[kOffGroupTransBatchCount - (kOffGroupBboxMinZ + sizeof(float))];
        uint16_t transBatchCount;   // kOffGroupTransBatchCount (MOGP+0x28)
        uint16_t intBatchCount;     // kOffGroupIntBatchCount
        uint16_t extBatchCount;     // kOffGroupExtBatchCount
        uint8_t  _pad62[kOffGroupLiquidType - (kOffGroupExtBatchCount + sizeof(uint16_t))];
        uint32_t liquidType;        // kOffGroupLiquidType (resolved LiquidType.dbc id)
        uint8_t  _pad148[kOffGroupBuffer - (kOffGroupLiquidType + sizeof(uint32_t))];
        uint32_t groupBuffer;       // kOffGroupBuffer -> group file buffer
        uint32_t groupSize;         // kOffGroupSize (group buffer byte size)
        void*    root;              // kOffGroupRoot -> parent root object
    };
    static_assert(offsetof(Group, flags)           == kOffGroupFlags,           "Group.flags");
    static_assert(offsetof(Group, bboxMinZ)        == kOffGroupBboxMinZ,        "Group.bboxMinZ");
    static_assert(offsetof(Group, transBatchCount) == kOffGroupTransBatchCount, "Group.transBatchCount");
    static_assert(offsetof(Group, intBatchCount)   == kOffGroupIntBatchCount,   "Group.intBatchCount");
    static_assert(offsetof(Group, extBatchCount)   == kOffGroupExtBatchCount,   "Group.extBatchCount");
    static_assert(offsetof(Group, liquidType)      == kOffGroupLiquidType,      "Group.liquidType");
    static_assert(offsetof(Group, groupBuffer)     == kOffGroupBuffer,          "Group.groupBuffer");
    static_assert(offsetof(Group, groupSize)       == kOffGroupSize,            "Group.groupSize");
    static_assert(offsetof(Group, root)            == kOffGroupRoot,            "Group.root");

    /**
     * @brief Placed WMO instance (spawned from one MODF record): the render/collision transforms, the
     *        owning root, and the doodad-set selection.
     */
    struct Instance
    {
        uint8_t  _pad00[kOffInstanceRenderMatrix];
        float    renderMatrix[16];     // kOffInstanceRenderMatrix (render rotation basis, row-major 4x4)
        float    collisionMatrix[16];  // kOffInstanceCollisionMatrix (collision/portal copy, TRANSPOSED)
        uint8_t  _padF0[kOffInstanceRoot - (kOffInstanceCollisionMatrix + 16 * sizeof(float))];
        uint32_t root;                 // kOffInstanceRoot -> owning root object
        uint8_t  _padF8[kOffInstanceDoodadSet - (kOffInstanceRoot + sizeof(uint32_t))];
        uint32_t doodadSet;             // kOffInstanceDoodadSet (selected doodad set index)
        uint8_t  _pad104[kOffInstanceExtraSets - (kOffInstanceDoodadSet + sizeof(uint32_t))];
        uint16_t extraSets[3];          // kOffInstanceExtraSets (extra doodad set indices, 0 = unused)
    };
    static_assert(offsetof(Instance, renderMatrix)    == kOffInstanceRenderMatrix,    "Instance.renderMatrix");
    static_assert(offsetof(Instance, collisionMatrix) == kOffInstanceCollisionMatrix, "Instance.collisionMatrix");
    static_assert(offsetof(Instance, root)            == kOffInstanceRoot,            "Instance.root");
    static_assert(offsetof(Instance, doodadSet)       == kOffInstanceDoodadSet,       "Instance.doodadSet");
    static_assert(offsetof(Instance, extraSets)       == kOffInstanceExtraSets,       "Instance.extraSets");

    /** @brief One MOBA render batch (record = batchArray + i * kMobaStride, batchArray = kGroupSlots[5].ptrField). */
    struct MobaRecord
    {
        uint8_t  _pad00[kOffMobaMaterialModern];
        uint16_t materialModern;    // kOffMobaMaterialModern (u16 material index, modern form)
        uint8_t  _pad0C[kOffMobaMaxIndex - (kOffMobaMaterialModern + sizeof(uint16_t))];
        uint16_t maxIndex;          // kOffMobaMaxIndex (last vertex index)
        uint8_t  flags;             // kOffMobaFlags (high nibble = frame scratch; bit kMobaFlagMaterialModern)
        uint8_t  material;          // kOffMobaMaterial (u8 material index, stock form)
    };
    static_assert(offsetof(MobaRecord, materialModern) == kOffMobaMaterialModern, "MobaRecord.materialModern");
    static_assert(offsetof(MobaRecord, maxIndex)        == kOffMobaMaxIndex,       "MobaRecord.maxIndex");
    static_assert(offsetof(MobaRecord, flags)           == kOffMobaFlags,          "MobaRecord.flags");
    static_assert(offsetof(MobaRecord, material)        == kOffMobaMaterial,       "MobaRecord.material");
    static_assert(sizeof(MobaRecord) == kMobaStride, "MobaRecord size/stride");

    /** @brief Group-info entry (root->mogiTable + i * kMogiStride): the per-group world AABB. */
    struct MogiEntry
    {
        uint8_t  _pad00[kOffMogiBbox];
        float    bboxMin[3];       // kOffMogiBbox (bbox min)
        float    bboxMax[3];       // bbox max (+0x10 within an entry)
    };
    static_assert(offsetof(MogiEntry, bboxMin)     == kOffMogiBbox,       "MogiEntry.bboxMin");
    static_assert(offsetof(MogiEntry, bboxMax)     == kOffMogiBbox + 0xC, "MogiEntry.bboxMax");
    static_assert(offsetof(MogiEntry, bboxMin) + 0xC == kOffMogiBbox + 0x10 - sizeof(float), "MogiEntry.bbox.layout");
    static_assert(sizeof(MogiEntry) <= kMogiStride, "MogiEntry fits stride");
#pragma pack(pop)

    // Allocators
    /// Hands back every root object the client will ever build, so an extension can over-allocate to
    /// append its own trailing per-root record. __cdecl, caller-cleaned.
    constexpr uintptr_t kAllocRoot                         = 0x007BFF20;
    /// The group-object allocator, the group twin of Wmo.AllocRoot. __cdecl, caller-cleaned.
    constexpr uintptr_t kAllocGroup                        = 0x007BFFE0;
    /// Paired release for Wmo.AllocInstanceGroup. __cdecl, caller-cleaned.
    constexpr uintptr_t kFreeInstanceGroup                 = 0x007C0370;
    /// Every placed WMO instance is born here, giving a single point to tag instances for later per-
    /// instance behaviour (scale, doodad-set override, culling policy). __cdecl, caller-cleaned.
    constexpr uintptr_t kAllocInstance                     = 0x007C03E0;
    /// The release edge that pairs with Wmo.AllocInstance, so instance-keyed tables never leak or alias
    /// a recycled pointer. __cdecl, caller-cleaned.
    constexpr uintptr_t kFreeInstance                      = 0x007C0430;
    /// Allocates the per-instance-per-group runtime record, the object interior/exterior visibility
    /// state actually hangs off. __cdecl, caller-cleaned.
    constexpr uintptr_t kAllocInstanceGroup                = 0x007C0910;

    // Area, zone and minimap queries
    /// Resolves a world position to the exact map object and group containing it, the primitive behind
    /// every WMO-aware area/zone/lighting decision. __thiscall, 5 stack args.
    constexpr uintptr_t kEntityResolveGroup                = 0x007A13E0;
    /// The interior zone name shown to the player, overridable per group for content the client's
    /// WMOAreaTable does not describe. __thiscall, 1 stack arg.
    constexpr uintptr_t kQueryZoneName                     = 0x007A1500;
    /// Maps a position inside a WMO to its WMOAreaTable row, so an extension can define new interior
    /// areas (and their zone text, music and lighting) for modern buildings. __thiscall, 3 stack args.
    constexpr uintptr_t kQueryAreaTable                    = 0x007A1640;
    /// Returns the WMO path the player is standing in -- the cheapest identification hook for an
    /// extension driving per-building behaviour from Lua. __thiscall, 1 stack arg.
    constexpr uintptr_t kQueryFileName                     = 0x007A1730;
    /// Supplies the interior minimap/floor descriptor for the current WMO, the hook for multi-level
    /// interior maps the client cannot express. __thiscall, 5 stack args.
    constexpr uintptr_t kQueryMinimapInfo                  = 0x007A17E0;
    /// Returns the (root id, group id, name-set id) triple identifying the current interior, the key an
    /// extension needs to look anything up per group. __thiscall, 3 stack args.
    constexpr uintptr_t kQueryIds                          = 0x007A18D0;
    /// Hands out the containing instance's world transform, which an extension needs to convert between
    /// building-local and world space (attachments, interior UI anchors). __thiscall, 2 stack args.
    constexpr uintptr_t kQueryMatrix                       = 0x007A1980;
    /// The per-group minimap resolution that decides which interior floor is shown, the level at which
    /// multi-storey interiors can be corrected. __thiscall, 8 stack args.
    constexpr uintptr_t kGroupMinimapQuery                 = 0x007AFC70;
    /// The root-level minimap descriptor, one level below Wmo.QueryMinimapInfo and independent of the
    /// entity. __thiscall, 6 stack args.
    constexpr uintptr_t kRootMinimapQuery                  = 0x007B00A0;
    /// Publishes the WMO world/view matrices used by the map-object draw, the seam for injecting a per-
    /// instance scale or a shadow-pass matrix override. __cdecl, caller-cleaned.
    constexpr uintptr_t kSetupInstanceMatrices             = 0x007F3E00;
    /// Parses one WMOAreaTable.dbc row, the place to rewrite or extend interior area records as they
    /// load instead of shipping a patched DBC. __thiscall, 2 stack args.
    constexpr uintptr_t kAreaTableRecordRead               = 0x008BD8C0;
    /// The WMOAreaTable.dbc lookup itself -- one detour lets an extension serve rows for modern WMOs
    /// that have no DBC entry, without touching the DBC. __cdecl, caller-cleaned.
    constexpr uintptr_t kAreaTableLookup                   = 0x00990560;

    // BSP and collision
    /// The BSP container's construction, the point to attach an extension-owned acceleration structure
    /// alongside the stock one. __thiscall, caller-cleaned.
    constexpr uintptr_t kBspConstruct                      = 0x0079B070;
    /// Releases the BSP nodes (and touches the global BSP digest cache at 0xCDD7A0), the release edge
    /// for a replacement BSP. __thiscall, caller-cleaned.
    constexpr uintptr_t kBspFree                           = 0x0079B0D0;
    /// Flushes the 0x1000-entry BSP digest cache, which an extension must do whenever it rebuilds or
    /// replaces group collision geometry at runtime. __thiscall, caller-cleaned.
    constexpr uintptr_t kBspDigestCacheReset               = 0x0079B1C0;
    /// Resets a group's BSP to empty, the seam at which an extension can install a rebuilt tree for a
    /// modern MOBN/MOBR pair. __thiscall, caller-cleaned.
    constexpr uintptr_t kBspClear                          = 0x0079B2C0;
    /// Extracts a placed instance's collision facets in world space, the form a navmesh or physics
    /// extension actually wants. __cdecl, caller-cleaned.
    constexpr uintptr_t kInstanceGetFacets                 = 0x007A4EE0;
    /// The map-wide WMO triangle query, the single entry an extension needs to snapshot all map-object
    /// collision geometry near a point. __cdecl, caller-cleaned.
    constexpr uintptr_t kQueryTrisForInstances             = 0x007A6940;
    /// The segment/ray-versus-WMO intersection used by picking and movement, so an extension can add
    /// per-material hit filtering or report a modern collision mesh. __thiscall, 9 stack args.
    constexpr uintptr_t kRootVectorIntersect               = 0x007AECB0;
    /// Root-level triangle collection across all groups, one call instead of iterating groups by hand.
    /// __thiscall, 4 stack args.
    constexpr uintptr_t kRootCollectTris                   = 0x007AEF00;
    /// The second root-level collector; covering both keeps an extension's geometry export complete.
    /// __thiscall, 4 stack args.
    constexpr uintptr_t kRootCollectTrisAlt                = 0x007AF0F0;
    /// The higher-level intersection dispatcher, cheaper to detour than Wmo.RootVectorIntersect when
    /// only the yes/no answer matters. __thiscall, 7 stack args.
    constexpr uintptr_t kRootIntersect                     = 0x007AF200;
    /// The per-node frustum predicate the BSP walk calls, so an extension can add an occlusion or LOD
    /// test at node granularity. __thiscall, 1 stack arg.
    constexpr uintptr_t kBspFrustumVolumeTest              = 0x007C7660;
    /// Converts a BSP query result into world-space triangles, the function to replace when a modern
    /// group's MOVI/MOVT layout differs from the client's assumed stride. __thiscall, 4 stack args.
    constexpr uintptr_t kGroupTrisFromQuery                = 0x007C7AE0;
    /// The BSP's box/frustum face-gathering traversal -- hooking it lets an extension answer collision
    /// and visibility face queries from a modern acceleration structure. __thiscall, 3 stack args.
    constexpr uintptr_t kBspQueryFaceIndices               = 0x007CA440;
    /// The box-query triangle collector for one group, the entry a physics or navmesh extension needs
    /// to pull WMO collision geometry. __thiscall, 5 stack args.
    constexpr uintptr_t kGroupCollectTris                  = 0x007CB180;
    /// Supplies the faces used to decide which world objects a group contains, i.e. the geometry behind
    /// "am I inside this building". __thiscall, 5 stack args.
    constexpr uintptr_t kGroupFacesForLinking              = 0x007CB260;
    /// Per-group intersection, the level at which an extension can exclude a group (a modern antiportal
    /// or a decorative shell) from collision. __thiscall, 5 stack args.
    constexpr uintptr_t kGroupIntersect                    = 0x007CB2F0;
    /// The second triangle-collection variant, needed alongside Wmo.GroupCollectTris to cover both
    /// query shapes without missing geometry. __thiscall, 5 stack args.
    constexpr uintptr_t kGroupCollectTrisAlt               = 0x007CB7B0;

    // Instance placement (MODF) and per-instance state
    /// Resolves a MODS doodad-set index to its record, so an extension can remap or synthesise a doodad
    /// set (including sets a modern root defines beyond the placed instance's selection). __thiscall, 1
    /// stack arg.
    constexpr uintptr_t kResolveDoodadSet                  = 0x007AEC30;
    /// Decides the footstep/material ground type under a point inside a map object, so an extension can
    /// supply modern material-driven surface types. __thiscall, 2 stack args.
    constexpr uintptr_t kInstanceGetGroundType             = 0x007B39B0;
    /// Rebuilds the MOLT-derived light set attached to one placed group, the hook for injecting modern
    /// interior lighting without touching the render path. __thiscall, caller-cleaned.
    constexpr uintptr_t kInstanceGroupUpdateLights         = 0x007B4090;
    /// The per-frame update of one instance's group record, the finest-grained per-group tick available
    /// on the instance side. __thiscall, 1 stack arg.
    constexpr uintptr_t kInstanceGroupUpdate               = 0x007B40F0;
    /// The per-group version of Wmo.InstanceSetSequence, letting an extension animate one wing of a
    /// building independently. __thiscall, 3 stack args.
    constexpr uintptr_t kInstanceGroupSetSequence          = 0x007B4170;
    /// Per-group event channel, the group-scoped twin of Wmo.InstanceSetEventCallback. __thiscall, 4
    /// stack args.
    constexpr uintptr_t kInstanceGroupSetEventCallback     = 0x007B4270;
    /// The instance object's own constructor, where its vtable is installed -- the point to swap in an
    /// extension-owned vtable for per-instance virtual behaviour. __thiscall, caller-cleaned.
    constexpr uintptr_t kInstanceConstruct                 = 0x007B4350;
    /// Drives the animation sequence of a WMO instance's doodads, the entry an extension needs to
    /// script building animations from Lua. __thiscall, 3 stack args.
    constexpr uintptr_t kInstanceSetSequence               = 0x007B45F0;
    /// Installs the client's own per-instance event callback, i.e. an existing, supported notification
    /// channel an extension can claim instead of detouring the event dispatcher. __thiscall, 4 stack
    /// args.
    constexpr uintptr_t kInstanceSetEventCallback          = 0x007B46A0;
    /// The per-instance streaming-readiness step, where an extension can force a WMO resident, defer
    /// it, or attach a load priority. __cdecl, caller-cleaned.
    constexpr uintptr_t kPrepareInstance                   = 0x007B5D00;
    /// The per-tick sweep over all placed WMO instances, the natural place to install a
    /// distance/priority policy for WMO streaming as a whole. __cdecl, caller-cleaned.
    constexpr uintptr_t kPrepareInstances                  = 0x007B6110;
    /// Fires whenever a placed WMO's transform changes (transports, moving platforms), the correct seam
    /// for keeping extension-side collision or shader state in sync. __cdecl, caller-cleaned.
    constexpr uintptr_t kInstanceUpdateMoved               = 0x007B64F0;
    /// The public "move this WMO instance" entry, so an extension can reposition map objects or veto a
    /// reposition. __cdecl, caller-cleaned.
    constexpr uintptr_t kInstanceUpdatePos                 = 0x007B66E0;
    /// Rebuilds the instance's render and collision bases, which is exactly where a per-instance scale
    /// (MODF+0x3E, ignored by the client) can be injected into both matrices at once. __cdecl, caller-
    /// cleaned.
    constexpr uintptr_t kInstanceUpdateMatrix              = 0x007B67B0;
    /// Binds one M2 doodad to a WMO instance, the seam for adding extension-spawned doodads that
    /// inherit the building's transform and lighting. __cdecl, caller-cleaned.
    constexpr uintptr_t kLinkDoodadToInstance              = 0x007B6800;
    /// Re-transforms every doodad of a moving WMO, the place to keep extension-spawned attachments
    /// riding a transport correctly. __cdecl, caller-cleaned.
    constexpr uintptr_t kMoveInstanceDoodads               = 0x007B68A0;
    /// Forces a WMO's doodad particle emitters to ignore the distance cutoff, the exact control needed
    /// to keep interior effects alive at range. __cdecl, caller-cleaned.
    constexpr uintptr_t kSetDoodadEmitterDistanceIgnore    = 0x007B69C0;
    /// The reverse binding -- given a doodad, find and attach the WMO instances containing it, so an
    /// extension can make an ADT-placed M2 inherit interior lighting. __cdecl, caller-cleaned.
    constexpr uintptr_t kLinkDoodadToInstances             = 0x007B6ED0;
    /// A supported per-instance doodad on/off switch an extension can drive from Lua for performance
    /// modes or cinematic staging. __cdecl, caller-cleaned.
    constexpr uintptr_t kSetDoodadsEnabled                 = 0x007B6F60;
    /// Builds the per-instance group records from the root's MOGI table, so an extension can add, drop
    /// or reorder the groups a placed instance exposes. __cdecl, caller-cleaned.
    constexpr uintptr_t kCreateInstanceGroups              = 0x007BDE50;
    /// Places a WMO instance from explicit position/rotation/flags rather than a MODF record, which is
    /// the entry an extension needs to spawn map objects at runtime (editor, dynamic set dressing)
    /// without forging a MODF blob. __cdecl, caller-cleaned.
    constexpr uintptr_t kSpawnInstanceExplicit             = 0x007BF120;
    /// The per-group doodad spawn loop -- the one place to filter, add or retarget the M2 doodads a WMO
    /// group instantiates, including honouring a modern doodad set the client would ignore. __cdecl,
    /// caller-cleaned.
    constexpr uintptr_t kSpawnGroupDoodads                 = 0x007BF740;
    /// The per-group spatial-link pass that decides which world objects a WMO group claims, the hook
    /// for changing interior/exterior object membership rules. __cdecl, caller-cleaned.
    constexpr uintptr_t kLinkIntersectInstanceGroup        = 0x007C1DC0;
    /// The moment a world object is bound to the WMO group it is standing in, i.e. the "who is inside
    /// this building" edge an extension can observe or override. __cdecl, caller-cleaned.
    constexpr uintptr_t kLinkObjectToInstanceGroup         = 0x007C1FF0;
    /// The per-instance level of the same spatial-link pass, one level above
    /// Wmo.LinkIntersectInstanceGroup. __cdecl, caller-cleaned.
    constexpr uintptr_t kLinkIntersectInstance             = 0x007C25D0;
    /// The top of the WMO spatial-link cascade, one call that covers every instance a query touches.
    /// __cdecl, caller-cleaned.
    constexpr uintptr_t kLinkIntersectInstances            = 0x007C2700;
    /// Per-group unload, finer-grained than Wmo.PurgeInstance. __cdecl, caller-cleaned.
    constexpr uintptr_t kPurgeInstanceGroup                = 0x007C3150;
    /// The instance unload path used by tile purge and map exit -- the reliable place to drop extension
    /// state without racing the allocator's recycling. __cdecl, caller-cleaned.
    constexpr uintptr_t kPurgeInstance                     = 0x007C3250;

    // Interior lighting and fog
    /// The positional lighting query used when linking objects to interiors, the hook that makes units
    /// and doodads inside a building pick up an extension's lighting. __thiscall, 5 stack args.
    constexpr uintptr_t kRootQueryLightingAt               = 0x007AEB40;
    /// The root-level lighting query that dispatches into the group query, cheaper to hook when a whole
    /// building should share one light override. __thiscall, 4 stack args.
    constexpr uintptr_t kRootQueryLighting                 = 0x007AF780;
    /// The function that evaluates a WMO group's interior lighting for a point (MOLT lights plus baked
    /// MOCV), the single place to replace stock interior lighting with a modern model. __thiscall, 4
    /// stack args.
    constexpr uintptr_t kGroupQueryLighting                = 0x007C7FE0;
    /// Binds a world light to every WMO instance it reaches, so an extension can inject dynamic lights
    /// that correctly affect building interiors. __cdecl, caller-cleaned.
    constexpr uintptr_t kLinkLightToInstances              = 0x007D9F90;

    // Load, parse and lifecycle
    /// Nulls every root chunk-slot pointer, so a post-hook here is the last quiet moment before the
    /// chunk walker starts filling them. __thiscall, caller-cleaned.
    constexpr uintptr_t kRootInitPointers                  = 0x007AE070;
    /// Fires on a freshly allocated root before any field is meaningful, the correct place to attach
    /// extension-owned per-root storage. __thiscall, caller-cleaned.
    constexpr uintptr_t kRootInitFields                    = 0x007AE300;
    /// The single point where a root's parsed state is torn down, so side-tables an extension keyed on
    /// the root pointer can be released exactly in step with the client's. __thiscall, caller-cleaned.
    constexpr uintptr_t kRootClear                         = 0x007AE3B0;
    /// The WMO subsystem's own bring-up, the earliest safe point to install pools, replacement tables
    /// or a render callback with the heaps already reachable. __cdecl, caller-cleaned.
    constexpr uintptr_t kSubsystemInit                     = 0x007AFEE0;
    /// The WMO root cache flush, so an extension holding root-keyed data can invalidate it at exactly
    /// the same instant the client does. __cdecl, caller-cleaned.
    constexpr uintptr_t kCachePurge                        = 0x007B0040;
    /// Paired shutdown for Wmo.SubsystemInit -- releases extension state before the client frees the
    /// WMO heaps out from under it. __cdecl, caller-cleaned.
    constexpr uintptr_t kSubsystemDestroy                  = 0x007B01C0;
    /// The root factory and cache-lookup front door -- every WMO root the map asks for by name passes
    /// here, so an extension can dedupe, substitute or refuse a root before it is ever allocated.
    /// __cdecl, caller-cleaned.
    constexpr uintptr_t kRootCreate                        = 0x007B0CC0;
    /// Clears every group sub-chunk slot, the reliable "group table is empty" edge for a replacement
    /// group walker. __thiscall, caller-cleaned.
    constexpr uintptr_t kGroupInitPointers                 = 0x007C7F10;
    /// The group's counterpart to Wmo.RootInitFields, and it is where the group format flags (including
    /// the two-UV bit at group+0x198) are seeded. __thiscall, caller-cleaned.
    constexpr uintptr_t kGroupInitFields                   = 0x007C9BC0;
    /// Per-group teardown, the release point for anything an extension allocated per group (replacement
    /// buffers, cached batch descriptors). __thiscall, caller-cleaned.
    constexpr uintptr_t kGroupClear                        = 0x007CBE80;
    /// Owns the moment the root file request is issued, so an extension can substitute the path,
    /// redirect to a modern root, or pre-stage its own buffer before any client parsing happens.
    /// __thiscall, 1 stack arg.
    constexpr uintptr_t kRootRead                          = 0x007D80C0;
    /// The per-group file request, giving a per-group substitution seam that `Wmo.RootRead` cannot
    /// reach because it fires once per root. __thiscall, 2 stack args.
    constexpr uintptr_t kGroupRead                         = 0x007D85E0;

    // Portals, bounds and visibility
    /// Builds the mask that tells collision and trace queries which WMO parts to skip, so an extension
    /// can define its own ignore categories for raycasts and pathing. __cdecl, caller-cleaned.
    constexpr uintptr_t kCreateIgnoreFlags                 = 0x007AE140;
    /// The residency predicate the render and cull paths gate on, so a background group loader can
    /// report "ready" on its own terms instead of the client's field. __thiscall, 1 stack arg.
    constexpr uintptr_t kIsGroupLoaded                     = 0x007AE4C0;
    /// The in-flight predicate that pairs with Wmo.IsGroupLoaded, needed to keep an async group
    /// loader's state machine consistent with the client's. __thiscall, 1 stack arg.
    constexpr uintptr_t kIsGroupLoading                    = 0x007AE4F0;
    /// The root AABB every visibility and streaming decision reads, so an extension can widen or
    /// tighten a WMO's effective bounds without touching the file. __thiscall, 1 stack arg.
    constexpr uintptr_t kGetRootBounds                     = 0x007AE5E0;
    /// The per-group AABB used by the group cull, the fix point for a modern group whose stored MOGI
    /// bbox is wrong or degenerate. __thiscall, 2 stack args.
    constexpr uintptr_t kGetGroupBounds                    = 0x007AE720;
    /// The single accessor 14 call sites use to read a group's MOGP flags, so one detour can synthesise
    /// the modern flag bits (exterior-lit, exterior-portal, antiportal) the client never sees.
    /// __thiscall, 1 stack arg.
    constexpr uintptr_t kGetGroupFlags                     = 0x007AE7B0;
    /// Tests a root against an arbitrary convex volume (the MCVP clip volumes), giving an extension a
    /// ready spatial predicate for custom queries or triggers. __thiscall, 1 stack arg.
    constexpr uintptr_t kTestConvexVolume                  = 0x007AEA10;
    /// Resolves a group's MOGN name, which is how antiportal groups are identified ("antiportal") -- an
    /// extension can rename or classify groups without editing MOGN. __thiscall, 1 stack arg.
    constexpr uintptr_t kGetGroupName                      = 0x007AEAE0;
    /// Returns a group's MOGI entry, the single indirection through which an extension can substitute a
    /// whole synthesised group-info record. __thiscall, 1 stack arg.
    constexpr uintptr_t kGetGroupInfo                      = 0x007AEB10;
    /// The whole-root readiness gate; a background loader must own it to avoid the client concluding a
    /// WMO is complete while groups are still in flight. __thiscall, caller-cleaned.
    constexpr uintptr_t kIsRootLoaded                      = 0x007AF740;
    /// The render-eligibility gate (materials and textures resident, not just parsed), the correct
    /// place to hold a WMO back until an extension's replacement textures land. __thiscall, caller-
    /// cleaned.
    constexpr uintptr_t kIsRootDrawable                    = 0x007AF850;
    /// Computes how far the camera is from the nearest exterior portal -- the scalar that drives
    /// interior/exterior blending, so an extension can retune the transition band. __thiscall, 6 stack
    /// args.
    constexpr uintptr_t kDistanceToExteriorPortal          = 0x007D77C0;
    /// The public-shaped wrapper for the same query, cheap to detour when only the final scalar
    /// matters. __stdcall, 3 stack args.
    constexpr uintptr_t kExteriorPortalDistanceQuery       = 0x007D8010;

    // Rendering
    /// Brackets the entire WMO render pass in one detour -- the natural place to set up or restore a
    /// whole-pass render state (depth prepass, custom shader collection, a scene-depth bind) for map
    /// objects only. __cdecl, caller-cleaned.
    constexpr uintptr_t kSceneRenderInstanceGroups         = 0x007964A0;
    /// The WMO half of the world cull, the point to add a modern occlusion test or to force instances
    /// visible without touching the terrain cull. __cdecl, caller-cleaned.
    constexpr uintptr_t kSceneCullInstanceGroups           = 0x0079A160;
    /// Draws only the collidable subset of a group's faces, giving an extension a per-face collision
    /// overlay it does not have to build itself. __stdcall, 1 stack arg.
    constexpr uintptr_t kRenderCollidableFaces             = 0x007A76C0;
    /// Draws the portal polygons themselves -- an extension gets a ready-made portal visualiser, and a
    /// place to render portal-space effects (interior fog volumes, door masks). __thiscall, 1 stack
    /// arg.
    constexpr uintptr_t kRenderPortalGeometry              = 0x007A9ED0;
    /// The WMO contribution to the shadow map, the hook for making buildings cast shadows under a
    /// replacement shadow technique (or excluding them cheaply). __cdecl, caller-cleaned.
    constexpr uintptr_t kRenderShadowMapGroups             = 0x007AB760;
    /// One call per rendered WMO group, sitting above the already-known exterior/interior batch leaves
    /// -- the right granularity for per-group state (a custom effect, a stencil mask, a per-group
    /// shader constant). __thiscall, 3 stack args.
    constexpr uintptr_t kRenderGroup                       = 0x007ABF50;
    /// The recursive portal-driven interior render itself, so an extension can bound recursion depth,
    /// widen a portal rect, or take over interior visibility wholesale. __thiscall, 5 stack args.
    constexpr uintptr_t kRenderThroughPortals              = 0x007AC060;
    /// The per-frame WMO pass setup, and the function that publishes the group-render function pointers
    /// -- hooking it lets an extension install its own group renderer through the client's own
    /// indirection instead of patching code. __cdecl, caller-cleaned.
    constexpr uintptr_t kPrepareUpdate                     = 0x007AD020;
    /// The single function that writes a WMO group's whole vertex buffer, so an extension can emit a
    /// modern vertex layout (second UV set, tangents, extra colour) instead of the stock one.
    /// __thiscall, 2 stack args.
    constexpr uintptr_t kGroupFillVertexBuffer             = 0x007C8560;
    /// Binds the group's vertex buffer for a draw -- the wrapper around Wmo.GroupFillVertexBuffer and
    /// the place to redirect a group onto an extension-owned buffer. __thiscall, caller-cleaned.
    constexpr uintptr_t kGroupSetVertexBuffer              = 0x007C9CB0;
    /// The index-buffer bind, the companion to Wmo.GroupSetVertexBuffer for substituting group
    /// geometry. __thiscall, caller-cleaned.
    constexpr uintptr_t kGroupSetIndexBuffer               = 0x007C9D80;
    /// The CPU-side vertex array allocation from the shared pool at 0xAEEEB0, where a modern group's
    /// larger vertex count must be accounted for. __cdecl, caller-cleaned.
    constexpr uintptr_t kGroupAllocVertexArray             = 0x007CB520;
    /// Allocates the group's GPU vertex buffer, so an extension can size it for a wider modern vertex
    /// format before anything is written into it. __thiscall, caller-cleaned.
    constexpr uintptr_t kGroupAllocVertexBuffer            = 0x007CBCB0;
    /// The paired release for Wmo.GroupAllocVertexBuffer, preventing an extension-owned buffer from
    /// outliving the group. __thiscall, caller-cleaned.
    constexpr uintptr_t kGroupFreeVertexBuffer             = 0x007CBD70;
    /// Allocates the WMO group's liquid vertex buffer, the sizing hook for modern (MLIQ-extended) water
    /// inside buildings. __thiscall, 4 stack args.
    constexpr uintptr_t kGroupAllocLiquidBuffer            = 0x007CBDC0;
    /// Paired release for Wmo.GroupAllocLiquidBuffer. __thiscall, caller-cleaned.
    constexpr uintptr_t kGroupFreeLiquidBuffer             = 0x007CBE30;
    /// The interior/exterior vertex-colour blend at portal transitions, the exact function to replace
    /// when a modern group's MOCV semantics differ from the client's assumption. __thiscall, 1 stack
    /// arg.
    constexpr uintptr_t kAttenuateTransitionVerts          = 0x007D78C0;

    // WMO liquids
    /// The per-group liquid-sound resolution beneath the root query, the finer point for per-group
    /// audio overrides. __thiscall, 2 stack args.
    constexpr uintptr_t kGroupQueryLiquidSounds            = 0x0079B760;
    /// Selects the ambient liquid sound for a WMO's water, so an extension can map a modern LiquidType
    /// to the right audio instead of falling back to silence. __thiscall, 6 stack args.
    constexpr uintptr_t kRootQueryLiquidSounds             = 0x0079BBF0;
    /// The map-wide "is this point in any WMO's liquid" query, one hook covering every placed instance.
    /// __cdecl, caller-cleaned.
    constexpr uintptr_t kMapQueryLiquidStatusInstances     = 0x007A09D0;
    /// Refreshes an entity's cached WMO-liquid state each tick, the seam for making a replaced interior
    /// water surface affect gameplay state. __thiscall, caller-cleaned.
    constexpr uintptr_t kEntityUpdateLiquid                = 0x007A1A30;
    /// Reports whether a point is in WMO liquid and which kind, the predicate that drives swimming and
    /// breath inside buildings. __thiscall, 4 stack args.
    constexpr uintptr_t kRootQueryLiquidStatus             = 0x007AEB90;
    /// The point query that reports the liquid surface inside a WMO group, so an extension can supply a
    /// modern liquid type or height where the stock MLIQ says nothing. __thiscall, 3 stack args.
    constexpr uintptr_t kGroupQueryLiquid                  = 0x007C8360;
    /// Counts the liquid tiles a group shares with terrain, the number that decides where WMO water is
    /// suppressed in favour of ADT water. __thiscall, caller-cleaned.
    constexpr uintptr_t kGroupSharedTileCount              = 0x007C8BF0;
    /// Generates the liquid vertex data at group load time -- the earliest place to rewrite interior
    /// water for a modern file, before any renderer sees it. __thiscall, caller-cleaned.
    constexpr uintptr_t kGroupGenLiquidVerts               = 0x007C8C60;
    /// Infers a liquid family from the MLIQ tile flags, the exact function that misclassifies a modern
    /// group's water and can be corrected in one detour. __thiscall, caller-cleaned.
    constexpr uintptr_t kIdentifyLegacyLiquidType          = 0x007C8D80;
    /// The per-liquid-tile refinement under Wmo.GroupLiquidVectorIntersect, the level at which an
    /// extension can honour modern per-tile liquid flags. __thiscall, 7 stack args.
    constexpr uintptr_t kGroupLiquidTileIntersect          = 0x007C8DD0;
    /// Builds the liquid surface triangles for a WMO group, the replacement point for a modern MLIQ
    /// layout or a higher-resolution water mesh. __thiscall, 4 stack args.
    constexpr uintptr_t kGroupLiquidTris                   = 0x007C94B0;
    /// Ray-versus-interior-water intersection, what an extension needs to make swim/submerge detection
    /// agree with a replaced water surface. __thiscall, 5 stack args.
    constexpr uintptr_t kGroupLiquidVectorIntersect        = 0x007C9DD0;
    /// The second liquid-triangle path; both must be covered for consistent interior-water geometry.
    /// __thiscall, 4 stack args.
    constexpr uintptr_t kGroupLiquidTrisAlt                = 0x007CAB70;
    /// The 1..20 legacy-family remap that produces the group's resolved LiquidType id, so an extension
    /// can pass a modern LiquidType through untouched instead of having it folded into a legacy family.
    /// __thiscall, 1 stack arg.
    constexpr uintptr_t kMapLegacyLiquidId                 = 0x007D7310;
}
