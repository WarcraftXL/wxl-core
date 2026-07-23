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
    // CMapObj::CreateData (0x007D7EB0), whose single caller it is.
    constexpr uintptr_t kRootComplete = 0x007D8050;
    // Group reader (group): the join point of both the sync and async group-load paths, run before the
    // group sub-chunk walk. Also a shim around the real sub-chunk walkers below; it decodes the 0x44-byte
    // MOGP header inline, then hands `groupBuffer + 0x58` to kGroupWalk.
    constexpr uintptr_t kGroupParse = 0x007D82E0;

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
    // texture_1 / texture_2 are byte offsets into MOTX on 3.3.5, and texture FileDataIDs on a modern
    // root (which ships no MOTX at all). The last 8 bytes are NOT file data: the client stores the two
    // live GPU texture handles there, which is why CreateMaterials zeroes them at load.
    constexpr size_t kMomtStride       = 0x40;
    constexpr size_t kOffMomtShader    = 0x04; // u32; CreateMaterial rewrites 3/5/6 -> 4 when tex2 is empty
    // Highest shader id this client has an effect for. The lookup behind it is UNCHECKED: a higher id
    // selects past the effect table, CShaderEffect::SetCurrent stores a null current effect, and the
    // next CShaderEffect::SetShaders faults on `DAT_00D43024 + 0x2C + index*4` near address 0.
    constexpr uint32_t kMaxClientShaderId = 6;
    constexpr size_t kOffMomtBlend     = 0x08; // u32
    constexpr size_t kOffMomtTexture1  = 0x0C; // u32 MOTX offset | modern FileDataID
    constexpr size_t kOffMomtTexture2  = 0x18; // u32 MOTX offset | modern FileDataID
    constexpr size_t kOffMomtHandle1   = 0x38; // runtime texture handle (0 = not loaded yet)
    constexpr size_t kOffMomtHandle2   = 0x3C; // runtime texture handle
    // Name CreateMaterial substitutes for an empty texture_1, and the global that disables the second
    // texture entirely when the shader pipeline is off.
    constexpr const char kFallbackTextureName[] = "createcrappygreentexture.blp";
    constexpr uintptr_t kShaderEffectsEnabled = 0x00D43020; // CShaderEffect::s_enableShaders (u32)

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

    // WMO batch-draw leaves (RE report: WMO Composite draw seam). Both are __thiscall(this=root, group,
    // int flag) with a `ret 8` epilogue, invoked through a vtable (no direct E8 caller). Each iterates the
    // group's MOBA batches and, per batch, activates the material's effect collection + binds VS/PS
    // (kEffectBind) then issues an indexed draw that flushes GxState. When root->MOHD.flags & 0x2 (the
    // modern "unified render path" bit -- set on 100% of the modern corpus) they tail-call the alternate
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
    // Instance transform matrices (4x4 row-major floats): +0x70 the render rotation basis, +0xB0 the
    // collision/portal copy. A fresh instance's basis is orthonormal; a uniform 3x3-row scale of both
    // resizes the rendered and the collided WMO together.
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

    /// ROOT slots, exactly as `kRootWalk` fills them. Order here is the canonical 3.3.5 walk order,
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
    constexpr size_t kMogiStride       = 0x20;  // group-info entry stride
    constexpr size_t kOffMogiBbox      = 0x04;  // bbox min within an entry (max at +0x10)

    // --- group object fields ---
    constexpr size_t kOffGroupBuffer = 0x184; // group buffer pointer
    constexpr size_t kOffGroupSize   = 0x188; // group buffer byte size
    constexpr size_t kOffGroupRoot   = 0x18C; // -> parent root object

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
    // Values the OUTDOOR branch of CWorldScene::Render writes into the five floats above: a full
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
    // 3.3.5 feeds those occluders to its ANGULAR clip buffer (384 slots) -- a coarse solid-angle test
    // with no depth. Legion instead rasterises the same meshes into a hi-Z SceneOcclusionBuffer, so a
    // large slab only hides what is genuinely behind its silhouette. A modern antiportal (the bridge's
    // is 173 x 48 units) therefore occludes far more on 3.3.5 than the artist ever intended.
    constexpr uintptr_t kCreateOccluders = 0x007D81C0;
    using Wmo_CreateOccludersFn = void(__fastcall*)(void* group, void* edx);
    /// Group-flag bit Legion sets on an antiportal group; present in the files, unknown to 3.3.5.
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
    // Legion re-enables the exterior from inside an interior group under three conditions that 12340
    // has no equivalent for (FUN_00CB5FEC). Measured on the active corpus: A and C never occur, B holds
    // for 37 of the 94 interior groups -- those are the groups where standing inside kills the terrain
    // here but not in Legion.
    constexpr uint32_t kGroupFlagExteriorLit   = 0x40;       // condition A
    constexpr uint32_t kGroupFlagExteriorPortal = 0x20000000; // condition C

    // --- camera-in-group containment (cull path) ---
    constexpr uintptr_t kBspRaycastRefine = 0x007CB0C0;
    // Group collision fields (group object). MOVI = u16[3] indices per face; MOVT = C3Vector vertices.
    constexpr size_t kOffGroupBsp       = 0x64;  // CAaBsp container (null when the group has no BSP)
    constexpr size_t kOffGroupMovi      = 0xE0;  // triangle vertex indices (u16[3] per face, stride 6)
    constexpr size_t kOffGroupMovt      = 0xE8;  // vertices (C3Vector, stride 0x0C)
    // MOVI holds one u16 index per triangle CORNER, so +0x154 counts INDICES (MOVI_size / 2), not faces.
    // The face count lives at +0x150 (MOPY_size / 2) -- proven by CreateOccluders, which walks faces with
    // the one and indices with the other. Reading +0x154 as a face count over-runs the array by 3x.
    constexpr size_t kOffGroupMoviCount = 0x154; // MOVI index count (MOVI_size / 2)
    constexpr size_t kOffGroupFaceCount = 0x150; // MOPY face count (MOPY_size / 2)
    // CAaBsp fields (relative to kOffGroupBsp).
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
    // Only RE'd fields are named; the gaps are explicit padding. Pointers are 4 bytes on the 32-bit client.
#pragma pack(push, 1)
    /** @brief Map-object root: the parsed root object that owns the material table and the group array. */
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
        void*    rootBuffer;       // kOffRootBuffer -> root file buffer
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

    /** @brief Map-object group: a runtime group object holding its file buffer and a back pointer to the root. */
    struct Group
    {
        uint8_t  _pad00[kOffGroupBuffer];
        void*    groupBuffer;      // kOffGroupBuffer -> group file buffer
        uint32_t groupSize;        // kOffGroupSize (group buffer byte size)
        void*    root;             // kOffGroupRoot -> parent root object
    };
    static_assert(offsetof(Group, groupBuffer) == kOffGroupBuffer, "Group.groupBuffer");
    static_assert(offsetof(Group, groupSize)   == kOffGroupSize,   "Group.groupSize");
    static_assert(offsetof(Group, root)        == kOffGroupRoot,   "Group.root");

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
}
