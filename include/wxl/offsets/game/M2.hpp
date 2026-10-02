// Model load / animation / batch-alpha / ribbon entry addresses, signatures, and object field offsets.
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

// INTERNAL to the core. Client addresses and runtime object field offsets. This is the private
// SOURCE the game-binding catalog is curated from; modules never include it, they call wxl::game.
namespace wxl::offsets::game::m2
{
    // --- load / setup ---
    // Model init: parses a model file and builds the runtime model.
    constexpr uintptr_t kInit = 0x0083CF00;
    // The async-read completion that reaches kInit, and the ONLY point at which a loader can still
    // defer that parse. Its whole body is:
    //     AsyncFileReadDestroyObject(model+0x0C); model+0x0C = 0; kInit(model);
    // so the "still loading" marker every consumer tests is already cleared by the time kInit runs --
    // a detour on kInit cannot background its work, because nothing would block on the result any more.
    // Registered as a function pointer (no direct callers), hence detour by address.
    // TRAP for a loader that DOES defer here: consumers do not go through any M2-specific wait helper.
    // They inline `if (model+0x0C) kAsyncFileReadWait(model+0x0C)` at ~30 sites, so holding the marker
    // non-null routes every waiter into kAsyncFileReadWait, which pumps the async drain. Deferred work
    // must therefore be reachable from THAT wait, never only from a per-frame drain of our own.
    constexpr uintptr_t kLoadSucceededCallback = 0x0083D340;
    using M2_LoadSucceededCallbackFn = void(__cdecl*)(void* model);
    constexpr size_t kOffModelAsyncRead = 0x0C; // -> the in-flight async-read object, null once complete
    // Skin-profile finalizer: runs once after the skin sub-arrays resolve and before the shader passes
    // size their batch blocks. The point to rebuild the material contract a modern skin omits.
    // CAUTION: calls kBuildBatchMaterial per batch. kBuildBatchMaterial crashes (EBX=0 null-deref at
    // 0x836D11) when M2Batch::shaderId has bit 0x8000 set AND (shaderId & 0x7FFF) > 3 -- a missing
    // switch case patched by our hkBuildBatchMaterial hook. Calling kFinalizeSkin a second time
    // also leaks model+0x18C (submesh copy), model+0x188 (per-submesh objects), [model+0x150]+0x40
    // (IB staging buffer) -- no free-before-write; acceptable for infrequent equip cycles.
    constexpr uintptr_t kFinalizeSkin = 0x00837A40;
    // Per-batch material-key builder: thiscall (ECX=model), 1 stack arg (batch ptr from skin->batches).
    // Called by kFinalizeSkin once per skin batch; result stored in model+0x188 array.
    // Reads M2Batch::shaderId (batch+2): bit 0x8000 selects the path; bits 0-14 index the switch.
    // Switch handles values 0-3 only; values > 3 with bit 0x8000 are an unimplemented case that
    // crashes. Hook (hkBuildBatchMaterial in GameHooks.cpp) returns nullptr to skip those safely.
    constexpr uintptr_t kBuildBatchMaterial = 0x00836C90;
    using M2_BuildBatchMaterialFn = void* (__fastcall*)(void* model, void* edx, void* batchPtr);

    // Version-gate branches in the loader. The stock loader accepts only one inner version; these are
    // the two compare branches that reject higher inner versions. Both read the MODEL body's inner
    // version (body+0x04) and demand exactly 264, so a body that states anything else is refused.
    constexpr uintptr_t kVersionGateInit = 0x0083CF51; // version-too-high branch
    // Inside kInitLowPrioritySequence: the gate a body must pass for a loaded .anim to be bound into
    // its sequence's track slots at all. Past it, that function rebases the slots of one sequence
    // index and CM2Shared__FinishLoadingLowPrioritySequence (0x0083CA90) then walks the aliasNext ring
    // from it, rebasing every other member of the ring with the same buffer.
    constexpr uintptr_t kVersionGateAnim = 0x0083C745; // anim-parse version branch
    // Binds a loaded .anim buffer into one sequence index's track slots: re-runs the header walk with
    // the sequence index and the buffer in globals 0xAF59D8 / 0xD41258, which makes every plain-array
    // reader a no-op and every per-sequence track slot of that index rebase onto the buffer.
    constexpr uintptr_t kInitLowPrioritySequence = 0x0083C6E0;
    // Builds the companion-file name for a sequence and queues the read. Follows aliasNext while the
    // sequence's flags carry 0x40, so an alias asks for the file of the ring member that has the keys.
    constexpr uintptr_t kLoadLowPrioritySequence = 0x0083DA10;

    // --- native modern-M2 direct-fill entry points (features/m2native) ---
    // The half of kInit that runs AFTER the header offset->pointer walk. Chooses the skin profile
    // from the device bone budget, calls kLoadSkinProfile (name-based "%s%02d.skin"), allocates the
    // texture-handle array at model+0x174 and creates one handle per texture entry (creates the
    // texture from the filename pointer; solid white when filename.count < 2), flags materials with
    // blend > 4, and counts billboarded bones into model+0x198. thiscall, no args, called exactly
    // once from kInit.
    constexpr uintptr_t kSharedInitialize = 0x0083CC80;
    using M2_SharedInitializeFn = int(__fastcall*)(void* model);

    // The client allocator (__stdcall(size, name, line, flags), ret 0x10) kInit uses for the
    // external-sequence bookkeeping array it stores at model+0x28 (count at +0x24, cursor zeroed at
    // +0x2C). A native reader replicating kInit's tail MUST use this exact allocator so the model
    // destructor's matching free call stays valid. Call-site constants replicated from kInit:
    // name ".\\M2Shared.cpp", line 0x2DC, flags 8.
    constexpr uintptr_t kSMemAlloc = 0x0076E540;
    using SMemAllocFn = void*(__stdcall*)(uint32_t size, const char* name, uint32_t line, uint32_t flags);
    constexpr size_t   kOffModelExtSeqCount  = 0x24; // uint32: sequences whose data streams from .anim
    constexpr size_t   kOffModelExtSeqArray  = 0x28; // -> allocator-owned u32 array (count * 4)
    constexpr size_t   kOffModelExtSeqCursor = 0x2C; // uint32: zeroed by kInit's tail

    // Per-field header readers driven by kInit's offset->pointer walk. Shared cdecl shape:
    //   int Read*(uint8_t* base, uint32_t size, void* header, M2Array* arr)
    // Each validates arr->offset (+ count * stride) against size, then rewrites arr->offset into
    // base + offset (a raw pointer) -- count == 0 zeroes the pointer. The track-bearing readers
    // (bones/colors/transparency/uvanim/attachments/events/lights/ribbons) additionally fix the
    // per-record M2Track outer arrays and, for sequences with flags bit 0x20 (data in-model), the
    // nested per-sequence inner arrays; a global-sequence track fixes all inners unconditionally.
    // ALL of these gate on the rebase global kSeqRebaseState == -1 (load-time walk); the .anim
    // completion path sets it to a sequence index to re-drive only that sequence's inner slots.
    // Strides are IDENTICAL between client 264 and source 272-274 for every reader listed here
    // which is what lets the native
    // MD21 reader drive the stock readers over a modern body. Cameras (0x74 vs 0x64) and particle
    // emitters (0x1EC vs 0x1DC) differ in stride and are NOT listed: the native reader must not
    // call 0x839EF0 / 0x83AF90 on a modern body. All prologues byte-verified against Wow.exe
    // (55 8B EC + "83 3D D8 59 AF 00 FF" cmp of the rebase global).
    constexpr uintptr_t kReadVertices     = 0x00835AE0; // stride 0x30
    constexpr uintptr_t kReadByteArray    = 0x00835B80; // stride 1 (name)
    constexpr uintptr_t kReadVector3      = 0x00835BD0; // stride 0xC (bounding vertices/normals)
    constexpr uintptr_t kReadInt32Array   = 0x00835C20; // stride 4 (global loops, materials)
    constexpr uintptr_t kReadAnimations   = 0x00835C70; // stride 0x40 + looping-id flag post-pass
    constexpr uintptr_t kReadInt16Array   = 0x00835DF0; // stride 2 (every lookup table)
    constexpr uintptr_t kReadTextures     = 0x00836B60; // stride 0x10 + per-texture filename fixup
    constexpr uintptr_t kReadEvents       = 0x00836E40; // stride 0x24 (track base, no values)
    constexpr uintptr_t kReadColors       = 0x00837EE0; // stride 0x28 (color + alpha tracks)
    constexpr uintptr_t kReadTransparency = 0x008382A0; // stride 0x14 (weight track)
    constexpr uintptr_t kReadBones        = 0x008385A0; // stride 0x58 (trans/rot/scale tracks)
    constexpr uintptr_t kReadUVAnimation  = 0x00838B10; // stride 0x3C (trans/rot/scale tracks)
    constexpr uintptr_t kReadAttachments  = 0x00839080; // stride 0x28 (animateAttached track)
    constexpr uintptr_t kReadLights       = 0x00839270; // stride 0x9C (7 tracks)
    // Camera reader, stride kCameraStrideClient (3 tracks). Valid on any body whose cameras carry
    // that layout, which is every body the native reader hands on.
    constexpr uintptr_t kReadCameras      = 0x00839EF0;
    // Ribbons (stride 0xB0, identical 264 vs 272-274): the reader IS the already-curated
    // kRibbonDeRelocate (0x83A460); reuse that constant.
    using M2_HeaderReadFn = int(__cdecl*)(uint8_t* base, uint32_t size, void* header, void* array);

    // Load-vs-rebase state global read by every header reader: 0xFFFFFFFF during the load-time walk
    // (fix outer arrays + in-model inners), a sequence index while kPerSeqDeReloc re-drives one
    // streamed sequence. The native reader runs from the kInit detour where it is always -1; the
    // address is curated for asserts/diagnostics only -- never write it.
    constexpr uintptr_t kSeqRebaseState = 0x00AF59D8;

    constexpr uintptr_t kSceneTriangleHitTest = 0x0081D510;
    using M2_SceneTriangleHitTestFn = int(__fastcall*)(
        void* scratch, void* edx, uint16_t* indexBegin, uint16_t* indexEnd, int vertexBase,
        float* point, int mode, int candidate, float* bestDepth, int currentHit);

    // .skin filename builder (pathStem, profileIndex, outBuf): copies the path, strips the extension,
    // appends the two-digit skin suffix. outBuf is a fixed-size engine buffer.
    constexpr uintptr_t kBuildSkinPath = 0x00835A80;
    // Skin-profile loader (model, profileIndex): builds the "%s%02d.skin" path, opens the file,
    // allocates the raw buffer (kBufferAlloc) at model+0x170, and submits an async whole-file read
    // whose completion runs kFinishLoadingSkinProfile below. Named-profile only; a caller with an
    // already-open buffer of its own (any filename) calls kFinishLoadingSkinProfile directly instead.
    constexpr uintptr_t kLoadSkinProfile = 0x0083CB40;
    // Size of the engine sibling-file path buffer the builders write into.
    constexpr uint32_t  kSkinPathBufSize = 0x108;
    // Parses the raw skin buffer already sitting at model+0x170 (fileSize bytes) in place -- the five
    // M2Array pairs at +4/+0xc/+0x14/+0x1c/+0x24 (vertexLookup/indices/bones/submeshes/batches,
    // strides 2/2/4/0x30/0x18) rewritten from file offsets to absolute pointers -- then calls
    // kFinalizeSkin and walks the model's own loaded-callback list. This is the synchronous half of
    // kLoadSkinProfile's async completion; callable directly for a buffer sourced any other way (a
    // sibling LOD skin, a name that doesn't fit "%s%02d.skin"). thiscall(model, fileSize) -> bool.
    constexpr uintptr_t kFinishLoadingSkinProfile = 0x00838490;
    using M2_FinishLoadingSkinProfileFn = int(__fastcall*)(void* model, void* edx, uint32_t fileSize);

    // --- ground-shadow pass (the "shadow swings with the camera" bug) -------------------------
    // Projected-decal draw entry. NOT a shadow function despite the name it once carried elsewhere --
    // it projects a DECAL onto M2 bodies (selection ring, pet ring, auto-track cursor, AoE spell
    // reticle). M2s only enter its caster list when the 4th argument of the projected-texture draw
    // call carries bit 0, and the shadow callback (0x0077F500) passes 0 or 2 -- so this is
    // unconditionally dead for shadows in every configuration. Kept because it IS the right hook for
    // decal-on-model work.
    constexpr uintptr_t kProjectedDecalDraw = 0x00829AA0;
    using M2_ProjectedDecalDrawFn = void(__fastcall*)(void* instance, void* edx);

    // The M2 ground shadow is drawn by kRenderBatchShadowMap (0x00829BA0), declared further down --
    // ALREADY HOOKED by features/m2compat/Bones.cpp (oversized-palette guard). Do not install a second
    // detour on it; MinHook returns MH_ERROR_ALREADY_CREATED and the loser silently observes nothing.
    // Chain: the shadow query's render step (0x007BBC50) -> this batch-list dispatcher (0x0082DA40)
    //     -> the per-batch shadow-map draw (0x00829E40) -> 0x00829BA0.
    //
    // That path uploads c31 itself from the model's own state AND its shader receives
    // c14..c16 = inverse(cameraView) * lightView, which already cancels the camera view the palette
    // carries. So the view/world inconsistency documented elsewhere is NOT the residual-swing cause
    // here -- do not "fix" the palette space on this path without new evidence.
    //
    // Batch-list shadow dispatcher(opaqueList, alphaList) -- __cdecl, single caller 0x007BC3BA in the
    // shadow query's render step. The coarse point for a global shadow on/off; carries no
    // per-instance context (the instance only becomes a register down in 0x00829E40).
    constexpr uintptr_t kShadowMapBatches = 0x0082DA40;

    // Device shader-constant unlock(which, firstConst, constCount) -- __stdcall. For which==0 it
    // only WIDENS the vertex-constant dirty window (min/max); it copies nothing and flushes nothing,
    // so calling it twice over the same range is harmless.
    constexpr uintptr_t kShaderConstUnlock = 0x00683580;
    using Gx_ShaderConstUnlockFn = void(__stdcall*)(int which, unsigned firstConst, int constCount);
    // Vertex-shader constant block base (the device's constant-lock call for slot 0 is a pure address
    // lookup returning this) and the bone-palette register c31 = base + 31*16.
    constexpr uintptr_t kVsConstBlock = 0x00C5EFE8;
    constexpr uintptr_t kVsConstC31   = kVsConstBlock + 0x1F0;
    // Global shader-enable flag. Written once at shader-system init from the model cache flags & 8, so
    // its value is 8 or 0 -- test for NON-ZERO, never == 1. Zero means the CPU pre-transform path,
    // where the shadow vertices carry no bone data and the palette fix does not apply.
    constexpr uintptr_t kEnableShaders = 0x00D43020;
    // 4x4 matrix multiply: __cdecl mul(out, a, b) -> out = a*b, row-vector convention (v' = v*M).
    constexpr uintptr_t kMatrixMul = 0x004C1F00;
    using C44_MulFn = float*(__cdecl*)(void* out, const void* a, const void* b);
    // 4x4 matrix inverse -- __thiscall(src in ECX, out on the stack), full general adjoint/determinant
    // inverse. Deliberately NOT 0x004C2FC0 (the affine-only inverse): that one is rigid-body only
    // (transpose + -t*R) and silently produces a wrong shadow for any placement carrying scale, which
    // doodads do.
    constexpr uintptr_t kMatrixInverse = 0x006A43A0;
    using C44_InverseFn = float*(__thiscall*)(const void* src, void* out);

    // --- particle emitter stride sites ------------------------------------------------------
    // A modern (inner version 272-274) M2 particle emitter record is 0x1EC bytes; the client's is
    // 0x1DC. Every field below 0x1DC is at an IDENTICAL offset in both -- the 16 extra bytes
    // (multiTexScrollMid/Range) are appended at the END.
    //
    // NOT APPLIED as patches. The native reader normalizes every emitter to 0x1DC at load, so no site
    // in the binary ever steps a 0x1EC record and all nine keep their stock immediate. The table is
    // curated because it is the complete, verified answer to "where does the binary hardcode the
    // emitter stride" (verified by scanning all of .text for the dword, not just these instruction
    // forms -- there is no tenth), which any future emitter-layout work needs.
    //
    // Recorded with each site: how the model header is reachable there, and what the original
    // instruction did -- the two facts a stride redirect would need. Flags are DEAD at all nine
    // (each is followed by another flag-setting op before any conditional branch).
    //
    // The version gate for the wide form is `header[0x04] > 271`. It is NOT `globalFlags & 0x200`:
    // not one of the 970 modern models sampled sets that bit.
    struct ParticleStrideSite
    {
        uintptr_t va;        ///< instruction address
        uint8_t   length;    ///< 6 or 7 bytes; the thunk call is 5, remainder padded with nop
        uint8_t   headerReg; ///< index into kStrideHeaderSrc below
        uint8_t   opKind;    ///< index into kStrideOp below
        int8_t    disp;      ///< frame displacement for the [ebp+disp] forms, else 0
    };

    // Where the MD20 header pointer lives AT each site, and what the original instruction did.
    // headerReg: 0=[ebp+disp] 1=ebx 2=esi 3=edx 4=ecx 5=edi
    // opKind:    0=imul esi,stride  1=add ecx,stride  2=add ebx,stride  3=add [ebp+disp],stride
    inline constexpr ParticleStrideSite kParticleStrideSites[] = {
        // The particle-emitter reader's BOUNDS CHECK (count*stride + ofs <= fileSize), at 0x0083AF90.
        // Header is in memory only here and no register is free; the thunk saves what it uses.
        { 0x0083AFBA, 6, 0, 0,  0x10 },
        // Same reader -- per-record de-relocation cursor. ebx reloaded every iteration.
        { 0x0083AFE7, 6, 1, 0,  0 },
        // The model's post-load sizing pre-pass, at 0x00832EA0. NOTE: this site is a merge point
        // for three branches, so a patch must not assume fall-through reachability.
        { 0x00832F18, 6, 2, 1,  0 },
        // Same pass -- main emitter-construction loop.
        { 0x00834124, 7, 3, 3, -0x1C },
        // Single-threaded per-frame particle animate, at 0x008309C0. x87 state is LIVE.
        { 0x008309EE, 6, 4, 0,  0 },
        // Worker-thread per-frame particle animate, at 0x0082D2F0. x87 state is LIVE.
        { 0x0082D6BA, 7, 3, 3, -0x0C },
        // The scene's per-frame animate walk, at 0x00821A20 -- the ONLY multi-model site: it walks a
        // chain through model->+0x30. ebx holds the CURRENT model's header on every path into the
        // emitter loop (reloaded at 0x00822AC6; the only intervening write is a 7-byte `lea ebx,[ebx]`
        // nop), so deriving per-site is correct here where an entry-detour stride would not be. x87 LIVE.
        { 0x00822CFB, 7, 1, 3, -0x0C },
        // Texture-replace entry, at 0x00825260 -- on demand.
        { 0x0082536E, 7, 1, 3, -0x04 },
        // Particle-color-replace entry, at 0x00825410 -- on demand.
        { 0x00825470, 6, 5, 2,  0 },
    };

    constexpr uint32_t kParticleStrideClient = 0x1DC; // v264
    constexpr uint32_t kParticleStrideModern = 0x1EC; // v272-274
    constexpr uint32_t kParticleModernMinVer = 272;   // gate: header[0x04] > 271

    // --- camera record stride by source era ---------------------------------------------------
    // Unlike the emitter, the wider source camera is not the client record with fields appended: the
    // fov float at +0x04 is GONE and an animated FoV track (0x14) is appended after the roll track, so
    // every field from +0x08 on sits 4 bytes earlier in the source form. 0x64 - 4 + 0x14 = 0x74.
    // The native reader reshapes each record to the client layout at load (fov filled from the first
    // key of the source track), so nothing downstream ever steps 0x74.
    constexpr uint32_t kCameraStrideClient = 0x64;  // v264
    constexpr uint32_t kCameraStrideModern = 0x74;  // v272-274
    constexpr uint32_t kCameraModernMinVer = 272;   // gate: header[0x04] > 271

    // --- packed multi-texture textureId read sites ------------------------------------------
    // With emitter flag 0x10000000 the textureId at record+0x16 is three packed 5-bit ids, so the raw
    // value (e.g. 5284) is far past the model's texture table. The stock client uses it as a FLAT
    // index and reads out of bounds -- InitializeLoaded then AddRefs table[hugeId] and faults.
    //
    // NOT APPLIED as patches: the native reader unpacks the field at load, so every read site sees a
    // flat in-range index. The two sites stay curated as the complete list of where the client reads
    // this field -- each is ONE COMPLETE INSTRUCTION, which is what would make a read-site redirect
    // safe without a full disassembly.
    //
    // id2/id3 drive multi-texture particle blending the target renderer has no path for, so id1 is the
    // whole of what this client can consume.
    constexpr uintptr_t kParticleTexIdInitLoaded  = 0x00833ED9; // mov ecx,[eax+0x174]  (6 bytes)
    constexpr uint32_t  kParticleTexIdInitLen     = 6;
    constexpr uintptr_t kParticleTexIdReplaceTex  = 0x00825349; // movzx eax,[ecx+edx+0x16] (5 bytes)
    constexpr uint32_t  kParticleTexIdReplaceLen  = 5;
    constexpr uint32_t  kParticleFlagMultiTex     = 0x10000000;

    // Z-source setter for a plane particle emitter -- 39 bytes, exactly two callers (0x00830BFD in the
    // single-threaded particle animate, 0x00833CF3 in the post-load sizing pass). Modern content
    // stores zSource = 255.0 as a DISABLED sentinel, but the emitter's particle-creation path
    // (0x009815C0) tests `zSource == 0.0` and treats any other value as a real point source: it
    // discards the verticalRange/horizontalRange emission cone and overwrites the spawn position with
    // a normalized (pos - (0,0,zSource)) vector. That is why modern fire drifts sideways instead of
    // rising. A large share of modern emitters carry 255.0.
    constexpr uintptr_t kSetZsource = 0x00978DA0;
    using M2_SetZsourceFn = void(__fastcall*)(void* emitter, void* edx, float zSource);

    // --- particle emitter instance layout -----------------------------------------------------
    // Filled by the model's post-load sizing pass from each emitter record's track key 0 (0x00833C00
    // rate via vtable+0x28, 0x00833C91 lifespan, 0x00833C9D lifespanVary) and re-fed every frame from
    // the instance's sampled cells. The emitter's texture-dimensions setter (0x00978C70) REQUIRES
    // power-of-two dimensions and derives from them the shift + reciprocals a flipbook cell is decoded
    // with: column = cell & (columns - 1), row = cell >> shift.
    constexpr size_t kOffEmitterCellShift     = 0x0C;  // log2(columns)
    constexpr size_t kOffEmitterCellWidth     = 0x10;  // 1 / columns
    constexpr size_t kOffEmitterCellHeight    = 0x14;  // 1 / rows
    constexpr size_t kOffEmitterParticlePool  = 0x34;  // CParticle2[], stride kParticleRecordStride
    constexpr size_t kOffEmitterLiveCount     = 0x50;
    constexpr size_t kOffEmitterLiveIndices   = 0x54;  // uint32[] into the pool, kOffEmitterLiveCount long
    constexpr size_t kOffEmitterRate          = 0x9C;
    constexpr size_t kOffEmitterRateVary      = 0xA0;
    constexpr size_t kOffEmitterLifespan      = 0xA4;
    constexpr size_t kOffEmitterLifespanVary  = 0xA8;
    // Material chosen by the model's post-load sizing pass from the record's blend mode and handed
    // over by the emitter's material setter (0x00978BF0): a blend-state index and the draw flags that
    // go with it. Bit kEmitterMaterialAlphaTest stays set for the two alpha-TESTED modes (opaque, alpha key)
    // and is cleared for every blended one.
    constexpr size_t   kOffEmitterBlendState     = 0xD0;
    constexpr size_t   kOffEmitterMaterialFlags  = 0xD4;
    constexpr uint32_t kEmitterMaterialAlphaTest = 0x4;
    constexpr size_t kOffEmitterHeadCellBlock = 0xEC;  // -> the record's head-cell count+offset pair
    constexpr size_t kOffEmitterAtlasRows     = 0x120;
    constexpr size_t kOffEmitterAtlasCols     = 0x124;
    constexpr size_t kOffEmitterFlags         = 0x134;
    constexpr size_t kOffEmitterParentModel   = 0x184; // frame of reference passed to EmitNewParticles
    constexpr uint32_t kParticleRecordStride  = 0x20;
    constexpr size_t kOffParticleAge          = 0x00;  // seconds; the particle dies once it reaches lifespan
    constexpr size_t kOffParticlePosition     = 0x04;
    constexpr size_t kOffParticleVelocity     = 0x10;

    // New-particle emission: per-frame, called from the per-frame step BEFORE the ageing pass, so a
    // replacement particle is born while the one it replaces is still alive. Accumulates rate * dt
    // and spawns round(accumulator + 0.5) particles, capped by the free list (the pool grows on
    // demand, so it is not a fixed ceiling).
    //
    // Unless kEmitterIgnoreDistance is set it first scales the rate by
    // 1 - (distance - 50) * 0.02 clamped to [kEmitterRateFloor, 1] -- distance being the camera
    // distance the emitter's per-frame update (0x0097EB10) publishes just before. NOTHING in the
    // model file maps onto that flag: the post-load sizing pass translates the record flags bit by
    // bit (0x00833D14-0x00833EAF) and never touches it. Its only setters are two map-placement
    // property setters (0x007B69C0, 0x0077FE80) -- it is a PLACEMENT property.
    //
    // The falloff assumes a dense emitter, where a quarter rate just thins the crowd. Source content
    // also authors single-billboard emitters (rate * lifespan near 1), for which the same cut stretches
    // the emission period past the lifetime and the effect goes dark between particles.
    constexpr uintptr_t kEmitNewParticles = 0x0097D8C0;
    using M2_EmitNewParticlesFn = void(__fastcall*)(void* emitter, void* edx, float dt, void* frame);
    constexpr uint32_t kEmitterIgnoreDistance = 0x400000;
    constexpr float    kEmitterRateFloor      = 0.25f;

    // Emitter pool sync -- vtable SLOT 0 of every emitter kind, called once per frame per enabled
    // emitter from the per-frame step before the emission pass. It sizes the particle pool as
    //   ROUND((rateVary + rate) * (lifespanVary + lifespan) * kEmitterPoolHeadroom)
    // and grows the pool to it through the pool-allocation call below; each child emitter then gets
    //   min(itsOwnSize * theParentSize, kEmitterPoolCeiling).
    // The 15% is headroom over the expected live count, so a particle's replacement can be born while
    // the particle it replaces is still fading. ROUND is round-to-nearest, so any emitter whose
    // expected count is under ~1.3 -- a single-billboard effect, one particle at a time -- rounds the
    // headroom away and lands on a pool of exactly 1. Its replacement then cannot be allocated until
    // the previous particle is freed, which turns the emission period into the LIFESPAN and leaves at
    // least one frame with nothing alive.
    constexpr uintptr_t kEmitterSync = 0x0097EDF0;
    using M2_EmitterSyncFn = void(__fastcall*)(void* emitter, void* edx);
    // SyncAllocation(count) grows the pool to count, and only ever grows: it early-outs when the
    // current size already covers the request, so re-requesting a larger size is safe and idempotent.
    constexpr uintptr_t kEmitterSyncAllocation = 0x0097E480;
    using M2_EmitterSyncAllocationFn = void(__fastcall*)(void* emitter, void* edx, uint32_t count);
    // The model's post-load sizing pass -- builds every emitter of a loaded model, then wires each
    // one's record pointers into it. Native this-in-ECX, no stack arguments. It maps the record's
    // blend mode through a SEVEN-case jump table (0x008344DC, source modes 0..6) into a device blend state;
    // anything past it takes the default arm and comes out as blend state 0 with the alpha-test flag
    // still set -- i.e. OPAQUE, which for a particle is a solid quad. Source mode 7 (added in 4.3.4)
    // lands there. The record's own field offsets are needed to tell that case apart afterwards,
    // because the default arm and a genuine mode 0 leave identical results behind.
    constexpr uintptr_t kInitializeLoaded = 0x00832EA0;
    using M2_InitializeLoadedFn = uint32_t(__fastcall*)(void* model, void* edx);
    constexpr size_t kParticleRecBlendMode = 0x028;
    constexpr size_t kParticleRecHeadCells = 0x13C; // what kOffEmitterHeadCellBlock points at
    constexpr uint8_t kParticleBlendModeMax = 6;    // highest source mode the jump table covers

    constexpr float    kEmitterPoolHeadroom = 1.15f;
    constexpr uint32_t kEmitterPoolCeiling  = 0x1000;
    constexpr size_t   kOffEmitterChildCount = 0x6C;
    constexpr size_t   kOffEmitterChildArray = 0x70; // INLINE array of particle emitter*, not a pointer

    // --- external animation ---
    // External-anim read-completion callback (node): runs once after the bytes are read and before the
    // per-sequence track offsets are rebased. __cdecl(node); the node's I/O record (kOffNodeRecord)
    // carries the resident buffer + size that the rebase then reads, so this is the point at which a
    // wrapped .anim payload can be unwrapped in place without touching the allocation pointer the
    // model destructor frees.
    constexpr uintptr_t kAnimLoadComplete = 0x0083D840;
    using M2_AnimLoadCompleteFn = void(__cdecl*)(void* node);
    // External-anim loader (model, seqIdx): resolves the sequence alias chain, builds the path, opens
    // the file, allocates a buffer, and schedules the async read whose completion rebases the tracks.
    constexpr uintptr_t kSequenceLoad = 0x0083DA10;
    // .anim filename builder (pathStem, id, subId, outBuf): copies the stem, strips the extension,
    // appends the id-subId anim suffix.
    constexpr uintptr_t kBuildAnimPath = 0x00835A20;
    // Per-sequence track de-relocator (model, seqIdx, buffer, size): validates the buffer and rebases
    // sequence seqIdx's track inner slots against it, then updates the sequence flags.
    constexpr uintptr_t kPerSeqDeReloc = 0x0083C6E0;
    // M2 buffer allocator (size, name, line): allocates size+0x10, returns a 16-aligned pointer carrying a
    // back-shift byte at [ptr-1]. This is the allocator the .m2 load buffer (model+0x150) uses, so a
    // replacement buffer must come from here for the model destructor's matching free to be valid.
    constexpr uintptr_t kAnimBufferAlloc = 0x0083DE50;
    constexpr uintptr_t kBufferAlloc     = 0x0083DE50; // alias: same allocator, used for buffer swaps
    constexpr uintptr_t kBufferFree      = 0x0083DE90; // free a kBufferAlloc pointer (recovers base via [ptr-1])
    using M2_BufferAllocFn = void*(__cdecl*)(uint32_t size, const char* tag, int line);
    using M2_BufferFreeFn  = void (__cdecl*)(void* ptr);

    // I/O record field offsets used by the rebase: the buffer base and its byte size.
    constexpr size_t kOffRecordBuffer = 0x04;
    constexpr size_t kOffRecordSize   = 0x08;
    // Load-node field: the I/O record pointer.
    constexpr size_t kOffNodeRecord   = 0x08;

    // --- per-batch alpha ---
    // Shared per-batch alpha/material/cull setter: chooses the alpha-test reference from the blend mode
    // and pushes it to the device.
    constexpr uintptr_t kSetupBatchAlpha = 0x0081FE90;
    constexpr uintptr_t kSortOpaqueGeoBatches = 0x0081EAD0;
    // Pushes the alpha-test reference to the device.
    constexpr uintptr_t kPushAlphaRef = 0x00873BA0;

    // SetupBatchAlpha draw-context fields (this = the draw context): the instance being drawn and the live
    // material the caller set. Instance -> model is kOffInstModel below.
    constexpr size_t kOffDrawCtxInstance = 0x60; // draw context -> render instance
    constexpr size_t kOffDrawCtxMaterial = 0x98; // draw context -> live material record
    // Material record: the blend mode (1 = alpha key).
    constexpr size_t kOffMaterialBlend   = 0x02;

    // --- bone palette (per-frame skinning matrices; the bone-physics hook point) ---
    // Per-instance bone-palette build (instance, ...): fills the bone matrices for one instance from
    // the current pose, each frame, before the batch draw uploads the palette to the vertex shader.
    // Called from two sites per collection model per frame:
    //   (a) 0x8309C0 (kUpdateAttachedModels, called from inside kM2PerFrameUpdate of the parent)
    //   (b) the outer scene-traversal loop at 0x821B4E (iterates the full scene linked list)
    // Site (b) fires AFTER the parent's kM2PerFrameUpdate completes (and therefore after the
    // OnM2PerFrameUpdate event). Hooking kBuildBonePalette and re-applying the CharSweep in the
    // POST-hook guarantees the bone copy is the last write before GPU upload, regardless of
    // scene-list ordering. Both sites use fastcall (ecx = instance) with 5 stack args; ret 0x14.
    constexpr uintptr_t kBuildBonePalette = 0x0082F0F0;
    // Single-bone fast-path palette build: same call shape (fastcall, 5 stack args, ret 0x14).
    // Instances with init-flag bit 0x1000 route here; the build is one matrix copy, so the cadence
    // skip never applies to it and the recomposition walk calls it directly for such children.
    constexpr uintptr_t kBuildBonePaletteSimple = 0x0082E140;
    // Call-only shape shared by kBuildBonePalette/kBuildBonePaletteSimple: thiscall (ecx = instance),
    // sceneCtx is the scene's own per-frame camera-relative context matrix (kOffSceneAnimateCtx),
    // scale3/translate3 are always {1,1,1}/{0,0,0} at every native call site, the two trailing floats
    // always 1.0f -- verified across all six native call sites (both functions, all three drivers:
    // the scene's threaded and non-threaded animate loops, and its animate-thread entry point).
    using M2_AnimateMTFn = void(__thiscall*)(void* instance, void* sceneCtx, float* scale3,
                                             float* translate3, float unk1, float unk2);
    constexpr uintptr_t kRenderBatchShadowMap = 0x00829BA0;

    // RenderBatchShadowMap's co-instance run list: drawList->data (the scene's flat sorted batch
    // array, the same array RenderModelBatchListShadowMap's own outer loop walks) is a flat array of
    // 3-dword records, stride kShadowRunStride, one record per co-instance slot. A run of N
    // co-instances occupies N CONSECUTIVE records starting at drawIndex: record[i] = {0: instance
    // pointer, 1: unused by this read, 2: requested co-instance count -- only the head record
    // (i == drawIndex) carries a meaningful count}. Confirmed via disasm at both ends:
    // RenderBatchShadowMap itself reads listData[drawIndex*3+2] before calling AllocInstances
    // (0x00829c1b), and its caller RenderModelBatchListShadowMap re-reads the SAME field right after
    // RenderBatchShadowMap returns (0x00829f18) to advance its own run cursor -- the exact
    // DrawBatchDoodad / M2Element+0x1C shape (kM2ElementRunLengthField, above), so a splitting hook
    // here must restore this field before returning, for the same reason. `instance`, `skinSection`,
    // `batchMode`, `skinBatch`, `previousSection` are all confirmed shared/fixed for the whole run
    // (instance->model's CM2Shared, derived from instance+0x2C, never varies per co-instance slot;
    // boneCount comes from the fixed skinSection) -- only drawIndex needs to change per sub-call.
    constexpr size_t kShadowRunStride     = 3; // dwords per co-instance run record
    constexpr size_t kShadowRunCountField = 2; // dword offset of the "requested count" field

    // AnimateMT's own particle-emitter tick, called unconditionally as a sibling step of the
    // bone-palette compose (not from any other site -- confirmed via callgraph, its only caller is
    // AnimateMT itself). Per emitter: evaluates the emitter's own file-side tracks (enabled/rate/
    // color/etc.) through FUN_0082b270 (bool tracks) / FUN_0082b340 (float tracks), each of which
    // blends against the attachment bone's CURRENT sequence-assignment state -- reading only
    // boneStates[bone].{blendWeight, blendSeq, assignedSeq, animIndex} (kOffRtBoneBlendWeight and
    // neighbors), matching exactly the fields NeedsFullPassReason's MidBlend/MidClamp checks already
    // gate the Composed decision on, and never a bone's composed world matrix (which ComposeOnly's
    // own per-bone loop keeps fresh regardless). Whenever those checks let a model reach Composed,
    // this same state is therefore already correct for this call too -- safe to invoke directly from
    // the throttled path instead of falling all the way back to a full AnimateMT pass.
    constexpr uintptr_t kAnimateParticlesMT = 0x0082D2F0;
    using M2_AnimateParticlesMTFn = void(__thiscall*)(void* instance);

    // The scene's per-frame animate walk: a flat singly-linked list of ROOT instances only
    // (kOffInstParent == 0 -- a non-root is animated via its parent's own attachment recursion, never
    // reached from this list directly). Distinct from kOffInstScene (instance -> its scene) and from
    // kOffInstAttachedNext (a SEPARATE sibling chain for attachment recursion) despite similar-sounding
    // names -- three different links on three different structs.
    constexpr size_t kOffSceneAnimateHead = 0x28; // on the scene: -> first root M2Instance in the walk
    constexpr size_t kOffInstAnimateNext  = 0x44; // on M2Instance: -> next root instance in the walk
    // The per-frame camera-relative context matrix (64-byte 4x4 matrix) the scene's animate preamble
    // builds ONCE before either its threaded or non-threaded per-model loop runs, and every model's
    // build that frame reads (read-only, never written by the palette-build entry points) -- safe for
    // any number of concurrent readers with no extra synchronization.
    constexpr size_t kOffSceneAnimateCtx  = 0x84;

    // The dedicated single-worker wake/join pair: the whole of the client's native "second thread" for
    // animation. BeginThread stashes a job (fn, arg) and signals ONE persistent worker; WaitThread
    // blocks until that worker signals back. Not a parameterized pool -- there is no worker-count
    // knob here, which is exactly why the wider pool this codebase adds is built separately
    // (AnimatePool.cpp) rather than reusing this pair for more than the stock 2-way split.
    constexpr uintptr_t kCacheBeginThread = 0x0081BFA0;
    // __fastcall + dummy edx: this codebase's standard idiom for a HOOKED thiscall function (see
    // kIsDrawable) -- BeginThread is detoured by AnimatePool.cpp, so its original-trampoline pointer
    // needs the same shape as the detour, unlike a call-only thiscall typedef.
    using M2_CacheBeginThreadFn = void(__fastcall*)(void* cache, void* edx, void* fn, void* arg);
    constexpr uintptr_t kCacheWaitThread  = 0x0081BFD0;
    using M2_CacheWaitThreadFn = void(__thiscall*)(void* cache);

    // Compiler-generated function-local-static lazy-init a UV pivot constant (0.5, 0.5, 0.0) inside
    // the model's texture-transform animate entry (0x0082D6F0): the guard flag (bit 0 of the dword at
    // kTexPivotFlag) is written BEFORE the three payload floats, so two threads racing the very first
    // texture-animated model after boot can observe the flag set with stale/zero floats still behind
    // it -- a real (if narrow, cosmetic, one-frame) data race under >2-way concurrency that the stock
    // 2-way split already carries today, just rarely enough to hit. AnimatePool.cpp pre-seeds all four
    // words once at install, before any worker thread exists, so the native lazy branch never has
    // anything left to race on.
    constexpr uintptr_t kTexPivotX    = 0x00D411D0;
    constexpr uintptr_t kTexPivotY    = 0x00D411D4;
    constexpr uintptr_t kTexPivotZ    = 0x00D411D8;
    constexpr uintptr_t kTexPivotFlag = 0x00D411DC;

    // Return addresses of the scene walk's per-model palette-build calls. Cadence skipping engages
    // only when the build was invoked from one of these three sites; every other caller (on-demand
    // rebuilds after a sequence change, attachment recursion, particle spawn models) expects a
    // fresh full pose and must always get one.
    constexpr uintptr_t kPaletteCallRetSceneA = 0x00821B53; // frame driver walk (also threaded main half)
    constexpr uintptr_t kPaletteCallRetSceneB = 0x00821BDD; // frame driver walk, second branch
    constexpr uintptr_t kPaletteCallRetWorker = 0x0081CEFF; // worker thread's half of the interleaved walk

    // Instance drawable-readiness check(this, param2, param3): native thiscall, 2 stack args, ret 8. A
    // streaming-readiness gate (model loaded, textures resident, attachments resident via bits at
    // instance+0x10) -- NOT a distance/size cull. Shared by 21 unrelated callers (character creation,
    // portraits, nameplates), so it must never be detoured unconditionally; see kDoodadDrainRetA/B
    // below for the two call sites worth gating on. Declared __fastcall with a dummy edx (this
    // codebase's standard idiom for a hooked thiscall function) since it IS hooked -- a call-only
    // thiscall wouldn't need it.
    constexpr uintptr_t kIsDrawable = 0x00824FC0;
    using M2_IsDrawableFn = int(__fastcall*)(void* instance, void* edx, int param2, int param3);

    // Return addresses of the scene's per-frame animate entry's (0x00821A20) two calls to
    // IsDrawable(0,0), inside the per-frame element-build drain over the doodad/element queue
    // (scene+0x2C). The world scene shares this queue between static doodads AND live units (players,
    // NPCs, mounts, spell visuals) -- confirmed: all instance creation funnels through the same scene
    // creation entry point on the world scene's single M2-scene instance -- so a screen-size cull
    // hooked here must ALSO gate on kOffInstOwnerFlags bit 0x20 (doodad-only, never set by any
    // live-instance creation path) before ever downgrading a TRUE readiness result to FALSE. Confirmed
    // by disassembly: both sites are `call kIsDrawable`, 5 bytes, return address = call site + 5.
    constexpr uintptr_t kDoodadDrainRetA = 0x00821C77;
    constexpr uintptr_t kDoodadDrainRetB = 0x00822AB5;

    // Batch-doodad-compatibility check(this, submeshFlags): native thiscall, 1 stack arg (byte
    // pointer), ret 4, returns bool in eax. Decides whether an instance can ride the hardware-instanced
    // batch-doodad draw path (shared alpha per whole batch) instead of a single non-instanced draw
    // (its own material-setup call, hence its own alpha) -- the lever a screen-size fade needs to
    // force a fading doodad onto the single-draw path so it can be given its own alpha independent of
    // the rest of its batch. Two callers confirmed by disassembly (the scene's per-frame animate entry
    // ~0x0082204A, the batch-doodad-compatible-count pass ~0x00827F3B); both call sites verified to
    // enter at x87 depth 0, so this has no FPU hazard to worry about in a detour.
    constexpr uintptr_t kIsBatchDoodadCompatible = 0x00824550;
    using M2_IsBatchDoodadCompatibleFn = int(__fastcall*)(void* instance, void* edx, uint8_t* submeshFlags);

    // Draw-context material setup(this): native thiscall, ZERO stack args, ret (bare, no pop). `this`
    // is a scene-render-shaped draw-context object, forwarded unchanged from the caller's own `this`
    // (the batch/batch-doodad/ribbon draw entries etc.) -- NOT an M2Instance/M2Model. Reads the current
    // draw element pointer at this+0x50 and the current M2 instance pointer at this+0x60 (both
    // confirmed by disassembly, read-only within this function), then hands {r,g,b,alpha} to the
    // shader's diffuse setter. The alpha it reads is at *(this+0x50) + 0xC (element+0xC): the model's
    // own native alpha (instance global alpha x color-track x weight-track -- death/spawn/spell
    // fades), computed independently of any visibility/distance test. Verified x87-depth-0 on entry
    // and on all three return paths -- safe to call original from a detour with no FPU save/restore
    // needed.
    constexpr uintptr_t kSetupMaterial = 0x0081FE90;
    using M2_SetupMaterialFn = void(__fastcall*)(void* renderCtx, void* edx);
    constexpr size_t kOffRenderCtxElement  = 0x50; // -> current M2Element (see kOffElementAlpha)
    constexpr size_t kOffRenderCtxInstance = 0x60; // -> current M2Instance
    constexpr size_t kOffElementAlpha      = 0x0C; // float, consumed by kSetupMaterial's diffuse setup
    // The element's own position within its skin's batch array -- confirmed by the draw dispatcher's
    // own (disabled) diagnostic log call, which formats this exact field as "index=%d" right before
    // the batch draws. Stable for the batch's lifetime; matches the index a skin-finalize pass used
    // when it built any of its own per-batch-index side tables, so a finalize-time batch tag can be
    // looked up again here at draw time with no extra bookkeeping.
    constexpr size_t kOffElementBatchIndex = 0x18;

    // --- track evaluators (sampled per bone / per light each frame from the bone-palette build) ---
    // Vec3 track evaluator (model, runtimeBone, track, out, baseValue): samples a translation/scale
    // track for the current animation index and writes the interpolated vec3 into out.
    constexpr uintptr_t kTrackEvalVec3 = 0x0082B0A0;
    // Quaternion track evaluator (model, runtimeBone, track, out, baseValue): samples a rotation track
    // for the current animation index and writes the interpolated quaternion into out.
    constexpr uintptr_t kTrackEvalQuat = 0x00828680;

    // --- matrix/vector helpers the palette build routes its math through -----------------------
    // The recomposition walk calls these same entries so a recomposed palette is bit-identical to a
    // fully built one; only a handful of scalar expressions are ever evaluated outside them.
    // Rotation-matrix build from a quaternion: this = destination matrix, one stack arg (quat).
    // Writes all 16 elements (row 3 = 0,0,0,1).
    constexpr uintptr_t kMatrixFromQuat = 0x004C1DE0;
    using C44_FromQuatFn = void*(__thiscall*)(void* mat, const float* quat);
    // Row scale: rows 0..2 multiplied component-wise by v.x / v.y / v.z.
    constexpr uintptr_t kMatrixScaleRows = 0x004C1B90;
    using C44_ScaleRowsFn = void(__thiscall*)(void* mat, const float* vec3);
    // Pre-translate: folds a translation (applied before the matrix) into the translation row.
    constexpr uintptr_t kMatrixPreTranslate = 0x004C1B30;
    using C44_PreTranslateFn = void(__thiscall*)(void* mat, const float* vec3);
    // In-place multiply: this = this * other (row-vector convention).
    constexpr uintptr_t kMatrixMulAssign = 0x004C2370;
    using C44_MulAssignFn = void(__thiscall*)(void* mat, const void* other);
    // Affine point transform: out = vec * mat, including the translation row. cdecl, returns out.
    constexpr uintptr_t kVec3Transform = 0x004C21B0;
    using C3_TransformFn = float*(__cdecl*)(float* out, const float* vec3, const void* mat);
    // In-place 3-component normalize.
    constexpr uintptr_t kVec3Normalize = 0x004C3600;
    using C3_NormalizeFn = void(__thiscall*)(float* vec3);

    // --- ribbon ---
    // Ribbon-emitter de-relocator: pointer-fixes each ribbon emitter's sub-array offsets.
    constexpr uintptr_t kRibbonDeRelocate = 0x0083A460;
    // M2ModelHeader::ReadParticleEmitters -- same (base, fileSize, header, arrayField) shape as the
    // other kRead* walkers, stride kParticleStrideClient (two hardcoded immediates: 0x0083AFBA bounds
    // check, 0x0083AFE7 cursor). Valid on any body whose emitters carry that width, which is every
    // body the native reader hands on.
    constexpr uintptr_t kReadParticleEmitters = 0x0083AF90;
    // Ribbon emitter draw (emitter, stateBlock): builds the strip and binds one texture per layer.
    constexpr uintptr_t kRibbonDraw = 0x00980B70;
    // Resolve a texture handle to the internal texture object the sampler bind expects.
    constexpr uintptr_t kTexResolve = 0x004B6CB0;
    // Bind a texture to a sampler selector (device, selector, resolvedTexture).
    constexpr uintptr_t kSamplerBind = 0x00685F50;
    // Sampler selectors for the engine bind path: s0 = 0x15, consecutive. The native ribbon loop binds
    // only s0; the extra layers of a multi-texture ribbon are bound to s1/s2 so they survive one pass.
    constexpr uint32_t kSamplerSelS1 = 0x16;
    constexpr uint32_t kSamplerSelS2 = 0x17;

    // --- attachment / render-context functions ---
    // CreateSceneModel(scene, path, flags): loads (or takes a shared reference to) the model named by
    // path and builds a fresh scene model for it, returning that model. It ALWAYS creates -- there is
    // no lookup or reuse of an existing one, so a caller that wants get-or-create semantics must keep
    // its own mapping and release what it no longer needs. A failed load falls back to the engine's
    // placeholder model rather than returning null.
    constexpr uintptr_t kCreateSceneModel   = 0x0081F8F0;
    // Deprecated spelling: the old name read as get-or-create, which this is not. Kept so no
    // published name disappears.
    constexpr uintptr_t kGetRenderCtx       = kCreateSceneModel;
    // AttachToScene(renderCtx, subObj, slot): attaches a collection-M2 render context to a scene slot
    // on the parent CharModelObject render context.
    constexpr uintptr_t kAttachToScene      = 0x00831630;
    // DetachSlot(subObj, slot): detaches the M2 bound to a scene slot, releasing its render context.
    constexpr uintptr_t kDetachSlot         = 0x00827560;
    // ReleaseRenderCtx(renderCtx): releases a render context obtained from GetRenderCtx. thiscall,
    // no stack args (ECX only): decrements a refcount at renderCtx+0 and tears the model down once it
    // reaches zero.
    constexpr uintptr_t kReleaseRenderCtx   = 0x00824ED0;
    using M2_ReleaseRenderCtxFn = void(__fastcall*)(void* renderCtx, void* edx);
    // SetSequenceCallback(renderCtx, fn, userData, extra) / SetEventCallback(renderCtx, fn, userData,
    // extra): thiscall (ECX=renderCtx) + 3 stack args. The doodad spawn path (kSpawnFromMDDF, see
    // offsets/game/Doodad.hpp) wires the event callback at spawn (userData=the doodad, extra=0); the
    // doodad teardown path (kDoodadPurge) clears both (fn=userData=extra=0) before releasing the
    // render context. A caller that swaps a live doodad's render context between models should
    // replay the clearing half of that wiring at minimum.
    constexpr uintptr_t kSetSequenceCallback = 0x00823FE0;
    constexpr uintptr_t kSetEventCallback    = 0x00824060;
    using M2_SetCallbackFn = void(__fastcall*)(void* renderCtx, void* edx, void* fn, void* userData, uint32_t extra);
    // SetBoneSequence(slot, seqId, prevSeqId, prevSubSeqId, blendTime, loop, primary). __thiscall,
    // ECX=the M2 instance, 7 stack params (ret 0x1c = 28 bytes). 67 real callers spanning character
    // creation/selection, hand micro-animations, and general gameplay code -- the client's
    // general-purpose "play sequence X on this model" entry point (also tentatively named
    // CM2Model__SetBoneSequence from an earlier high-confidence import; well corroborated by
    // call-site breadth), not something narrow to doodad respawn.
    //
    // Param order confirmed 2026-08-18 (wxl-equip-extension WXL-32/33) -- the original guess above
    // ("seqId, subSeqId, ...") had the first two backwards:
    //   - slot (Param1): a SLOT selector, -1 = primary. Same concept GetBoneSequenceId's own `slot`
    //     param uses. NOT part of the sequence id. Selects WHICH BONE this call targets -- it's the
    //     model's own key_bone_id (see kOffHdrKeyBoneCount/kOffHdrKeyBoneArray, i.e. keyBoneLookup,
    //     above): -1 shortcuts straight to bone index 0 without consulting that table at all; any
    //     other value is looked up through it (out-of-range or a -1 entry there both mean "no such
    //     bone on this model"). Entirely different axis from the primary/secondary split below
    //     (which one of the RESOLVED bone's own two sequence slots gets written) -- "primary" is
    //     unfortunately overloaded between the two, see that param for the disambiguation.
    //   - seqId (Param2): the actual sequence id being requested. Confirmed two independent ways:
    //     (a) this function's own entry logic checks THIS param against -1 to decide whether to
    //     early-out into UnsetBoneSequence; (b) traced through kResolveSequenceFallback into
    //     GetSequenceIndexByAnimId_Variation (0x8260c0), whose linear-scan path compares THIS exact
    //     value against each sequence record's seqId field (M2SequenceRec, below).
    //   - Internally resolves seqId via GetSequenceIndexByAnimId_Variation, with
    //     kResolveSequenceFallback consulted first -- confirmed to mean a sequence the target model
    //     doesn't define degrades gracefully (alias-chain resolution) rather than failing outright,
    //     verified against real content (WXL-33/34).
    //   - primary (Param7, renamed 2026-08-19 -- was guessed "reset"): NOT a reset flag. Confirmed via
    //     disassembly to be a dispatch switch, per the resolved bone (see slot above), between this
    //     function's own two already-named callees: true tail-calls kSetPrimaryBoneSequence
    //     (0x00826C40) and writes seqId straight into that bone's kOffRtBonePendingSeq field (the
    //     same field GetBoneSequenceId reads back); false tail-calls kSetSecondaryBoneSequence
    //     (0x00826DD0) instead, which drives the bone's separate upper-body/layered slot (per its own
    //     existing doc comment below) via different RuntimeBone fields (0x9C/0xA0/0xA4, not yet
    //     individually confirmed/named, plus a further shared-setup call into kSetupBoneSequence,
    //     0x00826B00). GetBoneSequenceId has NO path to read whatever the false/secondary branch
    //     sets -- confirmed as the root cause of a real observed bug: an emote that plays through the
    //     secondary slot (e.g. an upper-body-only emote played while the legs keep running on
    //     whatever the primary slot already had) is completely invisible to any code that only polls
    //     GetBoneSequenceId, even though it is genuinely playing. Whether the bone-selecting slot
    //     param (Param1) and this primary/secondary split compose freely (i.e. whether non-root bones
    //     also carry their own independent primary+secondary pair), and how to read the secondary
    //     sequence back at all, is open -- see the wxl-equip-extension ticket tracking this.
    //   - prevSeqId/prevSubSeqId/blendTime/loop -- still only best-guess from the one known real call
    //     site (a doodad respawn: SetBoneSequence(-1, 0, -1, 0, 1.0f, 1, 1), i.e. slot=-1, seqId=0/
    //     Stand, primary=1). NOT independently confirmed the way slot/seqId/primary now are.
    constexpr uintptr_t kSetBoneSequence = 0x00832AB0;
    using M2_SetBoneSequenceFn = void(__fastcall*)(void* instance, void* edx, uint32_t slot,
                                                     uint32_t seqId, uint32_t prevSeqId,
                                                     uint32_t prevSubSeqId, float blendTime,
                                                     uint32_t loop, uint32_t primary);
    // GetSequenceIndexByAnimId_Variation(header, seqId, variationSkip) -> local sequence-array index
    // (0xFFFF if not found). NOT a member-function-style call the way most of this file's functions
    // are: all 3 real parameters are stack args, callee-cleaned (single `ret 0xc`) -- at least one
    // caller (CM2Model__GetSequenceInfo) loads ECX before calling, but this function's own body
    // never reads ECX anywhere, so that load is coincidental/dead, not a real "this". Model any call
    // site as 3 plain stack args with the ECX/EDX slots unused (see the Fn typedef below), not the
    // instance-in-ECX pattern used elsewhere in this file.
    //
    // Confirmed 2026-08-18 (WXL-32/33/34) via direct disassembly: when the model has no hash table
    // (header+0x24 == 0 -- the case for small/custom models), does a straight linear scan over the
    // sequence array (base header->seqPtr, stride kSeqStride), comparing each record's seqId field
    // (M2SequenceRec::seqId, below) against the requested id -- this is the actual trace that
    // confirmed seqId's own record offset. Also separately walks each record's nextAnimation field
    // in a variationSkip-bounded loop while resolving a *variation* request -- this is what
    // confirmed nextAnimation's offset and chain-index role.
    constexpr uintptr_t kGetSequenceIndexByAnimIdVariation = 0x008260C0;
    using M2_GetSequenceIndexByAnimIdVariationFn = uint16_t(__fastcall*)(
        void* ecxUnused, void* edxUnused, void* header, uint32_t seqId, uint32_t variationSkip);
    // BindTexSlot(renderCtx, modelPtr): binds the M2 model resource to texture slot key 2 (main texture).
    constexpr uintptr_t kBindTexSlot        = 0x00825260;
    // LoadResource(path, flags): loads a texture/resource by virtual path through the texture-create path.
    constexpr uintptr_t kLoadResource       = 0x004B9760;
    // ReleaseResource(resource): releases a resource handle returned by LoadResource.
    constexpr uintptr_t kReleaseResource    = 0x0047BF30;

    // --- character-model slot hooks ---
    // Per-render-ctx per-frame update: fires once per visible M2 instance per frame, recursively
    // through the scene graph. Hooked to drive bone-matrix copy and geoset filtering.
    constexpr uintptr_t kM2PerFrameUpdate      = 0x00828A00;
    // CharModel equip-slot handler (cmo, modelSlot, itemDataPtr, postFlag): dispatches an item to
    // an internal model slot, building paths and loading the M2.
    constexpr uintptr_t kCharModelSlotDispatch = 0x004F2640;
    // CharModel equip-slot clear (cmo, equipSlotWow): clears the WoW equipment slot on the CMO,
    // detaching any attached M2 and releasing its render context.
    constexpr uintptr_t kCharModelSlotClear    = 0x004EE6D0;

    // --- runtime instance object fields ---
    // Creation-time "kind" flags, forwarded verbatim from the scene's model-creation flags argument by
    // the model's init entry and never rewritten afterward -- stable for the instance's whole lifetime.
    // Bit 0x1: set on a PARENT instance, its children derive their own view distance instead of
    //   inheriting the parent's (read by the palette build's distance step).
    // Bit 0x20: native semantics are "defer the post-load sizing pass to the first live pass" (a
    //   lazy-init flag), but empirically it is set ONLY by the map's doodad-placement creation family
    //   across all 50 scene model-creation call sites in the client -- never by unit/mount/missile/
    //   spell-visual/UI creation paths. Used as the doodad-vs-live-instance discriminator for the
    //   screen-size cull since the client's world scene drains doodads and live units through the
    //   same per-frame element-build queue.
    constexpr size_t kOffInstOwnerFlags     = 0x04;
    constexpr size_t kOffInstInitFlags      = 0x10;  // init flags (bit 0 = anim init done; bit 6 = char-select present)
    constexpr size_t kOffInstModel          = 0x2C;  // -> runtime model
    constexpr size_t kOffInstScene          = 0x28;  // -> the scene this instance belongs to
    constexpr size_t kOffInstCmdRingHead    = 0x34;  // deferred-command ring cursor; equal to the tail when idle
    constexpr size_t kOffInstCmdRingTail    = 0x38;
    constexpr size_t kOffInstLastAnimFrame  = 0x3C;  // uint32: scene frame this instance last animated on
    constexpr size_t kOffSceneClock         = 0x0C;  // uint32 on the scene: absolute animation clock (ms)
    constexpr size_t kOffSceneFrame         = 0x14;  // uint32 on the scene: the current frame counter
    constexpr size_t kOffInstParent         = 0x48;  // -> parent M2 instance (null for root)
    constexpr size_t kOffInstAttachEnable   = 0x4C;  // -> per-attachment enable records (stride 0xC, u8 at +0x8)
    constexpr size_t kOffInstAttachSlot     = 0x54;  // uint32: attachment index this instance hangs on (0xFFFF = none)
    constexpr size_t kOffInstAttachedHead   = 0x58;  // -> first attached child instance
    constexpr size_t kOffInstAttachedNext   = 0x60;  // -> next sibling in the parent's attached-child list
    constexpr size_t kOffInstFreezeAnchor   = 0x64;  // uint32: nonzero arms externally driven pose freezing
    constexpr size_t kOffInstViewDistSq     = 0x88;  // float: squared view-space distance (also the draw sort key)
    constexpr size_t kOffInstConstTrackGate = 0x90;  // uint32: once-only constant-track sampling gate
    constexpr size_t kOffInstBoneStates     = 0x94;  // -> runtime bone-state block (stride kRuntimeBoneStride)
    // -> uint32[skin->submeshCount], one per submesh, allocated ALL-VISIBLE at initialize. Its only
    // writer is kSetGeometryVisible, so a submesh whose id no caller ever names stays drawn forever.
    constexpr size_t kOffInstSectionVisible = 0x9C;
    constexpr size_t kOffInstTexBinding     = 0x174; // shared texture-binding slot mirrored from the parent
    constexpr size_t kOffInstSpeedBase      = 0x178; // float: base playback speed
    constexpr size_t kOffInstAlphaBase      = 0x17C; // float: base alpha factor
    constexpr size_t kOffInstScaleBase      = 0x180; // float[3]: base scale
    constexpr size_t kOffInstTransBase      = 0x18C; // float[3]: base translation
    constexpr size_t kOffInstSpeedStage     = 0x198; // float: staged speed consumed by children/particles
    constexpr size_t kOffInstAlphaStage     = 0x19C; // float: staged alpha
    constexpr size_t kOffInstScaleStage     = 0x1A0; // float[3]: staged scale passed into child recursion
    constexpr size_t kOffInstTransStage     = 0x1AC; // float[3]: staged translation

    // Init-flag bits (kOffInstInitFlags) the palette build and its cadence logic consult.
    constexpr uint32_t kInstFlagLive       = 0x1;      // instance participates in animation at all
    constexpr uint32_t kInstFlagAnimDirty  = 0x400;    // pending state change; cleared by the full build tail
    constexpr uint32_t kInstFlagSimplePath = 0x1000;   // routed to the single-bone fast-path build
    constexpr uint32_t kInstFlagChildBase  = 0x40000;  // unslotted child rides the parent's palette slot 0
    constexpr uint32_t kInstFlagFixedTrans = 0x80000;  // staged translation ignores the caller's offset
    constexpr uint32_t kInstFlagFixedSpeed = 0x100000; // staged speed ignores the caller's multiplier
    constexpr size_t kOffInstBonePalette    = 0x98;  // -> bone matrices, row-major 4x4
    constexpr size_t kBonePaletteStride     = 0x40;  // one bone matrix
    // Model->world placement, an INLINE 64-byte matrix (the shadow loop reads &instance+0xb4).
    constexpr size_t kOffInstPlacement      = 0xB4;
    // Instance root, an INLINE 64-byte matrix written by the model's threaded animate entry
    // (0x0082F0F0) as placement * viewRoot -- i.e. VIEW space. Every palette slot is B_bone * this, so
    // the model-space factor of a bone is palette[b] * inverse(this) (palette FIRST: row-vector convention).
    constexpr size_t kOffInstViewRoot       = 0xF4;
    // Array of render-object pointers (4 bytes each), indexed by the skin profile's batch index.
    // Read by the per-frame update (0x828A00) to call the visibility pass (0x97f570) that controls
    // per-batch draw visibility.
    constexpr size_t kOffInstRenderObjArray = 0x2BC; // -> void*[] (one pointer per skin batch)
    // Per-instance lighting-callback pair: +0x2AC a function pointer, +0x2B0 its userData. Wired at
    // spawn (CMap::CreateDoodadDef, 0x007BECD0) to CMapStaticEntity::ModelLightingCallback
    // (kModelLightingCallback below) with userData = the owning doodad, and invoked every frame by
    // CM2Model::SetupLighting (0x00831af0) as (*fn)(instance, instance+0x1D4, userData) whenever
    // non-null. CMapDoodadDef::Purge (0x007C3020) nulls both fields before releasing an instance's
    // render context -- a caller that tears an instance down outside that path must do the same.
    constexpr size_t kOffInstLightingCallbackFn = 0x2AC;
    constexpr size_t kOffInstLightingUserData   = 0x2B0;
    // CMapStaticEntity::ModelLightingCallback -- the value CreateDoodadDef writes into
    // kOffInstLightingCallbackFn. Stored/compared as a raw pointer only; never called directly here,
    // so no calling-convention typedef is declared for it.
    constexpr uintptr_t kModelLightingCallback  = 0x00780CD0;
    // -> per-instance batch/section override arrays ([0] = batches, [2] = sections). When non-null,
    // the shadow-map batch-list draw (0x00829E40) resolves the shadow draw's section from here
    // INSTEAD of the model's shared+0x18C runtime copy -- so any fixup applied to that copy (notably
    // the boneInfluences 0 -> 1 lift at skin finalize) is bypassed for such an instance.
    constexpr size_t kOffInstSectionOverride = 0x2D0;
    // Visibility flags written by the visibility pass (0x97f570) into each render object.
    // Bit 2 = 1 -> batch visible; bit 2 = 0 -> batch hidden (bit 0 also cleared when hiding).
    constexpr size_t kOffRenderObjFlags     = 0x160;
    constexpr size_t kOffInstShared         = 0x2C;  // -> the shared model data this instance draws
    // Per-emitter blocks, both sized by the header's emitter count and built by the model's post-load
    // sizing pass: the sampled track cells it feeds the emitters from each frame, then the particle
    // emitter pointer array itself.
    constexpr size_t kOffInstEmitterCells   = 0x2C0; // stride kEmitterCellStride
    constexpr size_t kOffInstEmitterArray   = 0x2C4; // -> particle emitter*[]
    constexpr size_t kEmitterCellStride     = 0x88;
    constexpr size_t kOffCellLifespan       = 0x44;
    constexpr size_t kOffCellRate           = 0x50;
    constexpr size_t kOffCellEnabled        = 0x80;

    // --- runtime model object fields ---
    constexpr size_t kOffModelFlags         = 0x08;  // bit 2 selects the sibling-file open flag
    constexpr size_t kOffModelPathStem      = 0x3C;  // model path stem (no extension)
    constexpr size_t kOffModelHeader        = 0x150; // -> raw .m2 file buffer (parsed in place -> becomes the header)
    constexpr size_t kOffModelFileSize      = 0x16C; // byte size of the .m2 file buffer at +0x150
    constexpr size_t kOffModelSkin          = 0x170; // -> live parsed skin profile (valid at/after skin finalize)
    constexpr size_t kOffModelSubMeshCopy   = 0x188; // -> per-submesh object array ptr (written by kFinalizeSkin; not freed on re-call)
    constexpr size_t kOffModelSubmeshBuf    = 0x18C; // -> submesh copy buffer ptr (written by kFinalizeSkin; not freed on re-call)
    // uint32: how many CO-INSTANCE copies of the whole vertex array the shared vertex buffer holds.
    // kFinalizeSkin seeds it at 1, and kSharedSetVertices both sizes the pool by it
    // (skin->vertexCount * this * kModelVertexStride) and loops over it, writing copy N at
    // N * skin->vertexCount * kModelVertexStride with every bone slot shifted by
    // section->boneCount * N. Reading it as a done-flag happens to work only because the seed is 1.
    constexpr size_t kOffSharedCoInstanceCount = 0x190;
    // uint16: count of bones carrying flags & 0x2F8 (0x200 "transformed" + the billboard bits 0xF8),
    // tallied by the shared-data init entry (0x0083CC80). Its ONLY reader is the shadow-path animate
    // (0x00831990), which rebuilds the bone palette at instance+0x98 only when
    // `instance->parent != 0 || this != 0`; otherwise it takes a fast path that leaves the palette
    // holding an OLDER frame's view matrix. The shadow query's render step meanwhile uploads
    // c14..c16 = inverse(CURRENT view) * lightView, so a stale palette no longer cancels and the
    // shadow rotates by the camera's motion since that frame. Older-format exporters bake 0x200 into
    // practically every bone, so stock content always takes the full path; newer-format content ships
    // all bone flags 0x0 and would take the fast path -- hence the swing.
    constexpr size_t kOffSharedAnimGateCount = 0x198;
    // uint32: MAX INSTANCES per batched draw, computed by kFinalizeSkin as
    //   min(65536 / skin->indices.count, min over submeshes of skin->boneCountMax / submesh.boneCount), floored at 1
    // i.e. how many copies of the model fit in one 16-bit index buffer AND in the bone-constant palette.
    // Consumers: the shared-data instance allocator (0x00836DF0) clamps its grow request to it, and
    // the post-load sizing pass (0x00832EA0) clears the "batchable doodad" flag 0x10 when it is < 2
    // (cf. the batch-doodads CVar). NOTHING here is level-of-detail: the target build has no per-frame
    // M2 LOD at all.
    constexpr size_t kOffSharedMaxInstances = 0x194;
    // Deprecated spelling kept so no published name disappears; the "LodMultiplier" reading was wrong.
    constexpr size_t kOffModelLodMultiplier = kOffSharedMaxInstances;

    // --- parsed file-header fields ---
    constexpr size_t kOffHdrGlobalFlags    = 0x10; // bit 0x20 = model carries physics
    // Global-flag bit 0x4: the staged speed/scale/translation ignore the caller's arguments and
    // come from the instance's base fields alone (the palette build's alternate staging branch).
    constexpr uint32_t kHdrFlagFixedStaging = 0x4;
    constexpr size_t kOffHdrSeqCount       = 0x1C; // sequence records
    constexpr size_t kOffHdrSeqPtr         = 0x20; // -> sequence records (stride kSeqStride)
    constexpr size_t kSeqStride            = 0x40;
    constexpr size_t kOffSeqFlags          = 0x0C; // bit 0x1 = plays once to its end, then holds
    // Full sequence-record layout confirmed 2026-08-18 (wxl-equip-extension WXL-34): a live
    // in-memory record for a real loaded model (helm_robe_raidwarlock_f_01_gnf.m2) diffed
    // byte-for-byte IDENTICAL against that same model's own .m2 file at its own on-disk sequence
    // array offset. This record type carries no pointers/arrays needing load-time fixup, so the
    // file format (community-documented, see wowdev.wiki's M2 "sequences" chunk) applies unchanged
    // to the parsed runtime array -- every offset below is a confirmed BYTE POSITION. Independently
    // re-derived from this project's own disassembly (not just carried over from the file-format
    // doc): kOffSeqId (GetSequenceIndexByAnimId_Variation's match key), kOffSeqLength (read+
    // returned by GetSequenceInfo; WXL-34's live duration-gated playback also functionally confirms
    // milliseconds), kOffSeqFlags above (bit 0x40 = alias -- verified against real content: a
    // record with the bit set has its kOffSeqAliasNext pointing at a record without it, a real
    // terminus), kOffSeqBBoxMin/Max/Radius (GetSequenceInfo's bounding-sphere center/radius calc),
    // and kOffSeqNextAnimation (the variation-chain index GetSequenceIndexByAnimId_Variation
    // walks). kOffSeqSubId/Frequency/RangeMin/RangeMax/BlendTime/AliasNext carry the
    // community-documented field names for their now-confirmed positions, not independently
    // re-traced through this project's own disassembly.
    constexpr size_t kOffSeqId             = 0x00; // uint16: the value SetBoneSequence/lookup functions match against
    constexpr size_t kOffSeqSubId          = 0x02; // uint16
    constexpr size_t kOffSeqLength         = 0x04; // uint32, milliseconds
    constexpr size_t kOffSeqMovingSpeed    = 0x08; // float
    constexpr size_t kOffSeqFrequency      = 0x10; // int16
    constexpr size_t kOffSeqRangeMin       = 0x14; // uint32
    constexpr size_t kOffSeqRangeMax       = 0x18; // uint32
    constexpr size_t kOffSeqBlendTime      = 0x1C; // uint32
    constexpr size_t kOffSeqBBoxMin        = 0x20; // float[3]
    constexpr size_t kOffSeqBBoxMax        = 0x2C; // float[3]
    constexpr size_t kOffSeqBBoxRadius     = 0x38; // float
    constexpr size_t kOffSeqNextAnimation  = 0x3C; // int16: variation-chain index (0xFFFF/-1 = none)
    constexpr size_t kOffSeqAliasNext      = 0x3E; // uint16: alias-chain target when kOffSeqFlags bit 0x40 is set
    constexpr size_t kOffHdrBoneCount      = 0x2C;
    constexpr size_t kOffHdrBoneArray      = 0x30; // -> bone records (post-fixup data ptr)
    // keyBoneLookup (confirmed 2026-08-19, wxl-equip-extension WXL-38): the table SetBoneSequence/
    // GetBoneSequenceId's own `slot` param resolves through when slot != -1 (matches the community-
    // documented key_bone_lookup[] -- ArmL=0, ArmR=1, ..., Root=26, etc.; -1 = "no bone" per entry,
    // NOT the same -1 as the slot param's own "skip the lookup, use bone 0 directly" shortcut).
    // Confirmed via a live-memory-vs-file byte diff against a real player model (count=27, matching
    // the community doc's own stated WotLK count exactly, every entry a sane in-range bone index) --
    // NOT the same table as kOffHdrBoneIdxLutCount/Ptr below (0xF8/0xFC, already in core, used for
    // equipment-to-character bone REMATCHING by BuildBoneRemap): the two were cross-checked against
    // the same real model file and are genuinely different arrays (27 sparse entries vs 55 with a
    // markedly different id-to-bone pattern), not two names for the same data.
    constexpr size_t kOffHdrKeyBoneCount   = 0x34;
    constexpr size_t kOffHdrKeyBoneArray   = 0x38; // -> int16 array, -1 = this model has no such key bone
    constexpr size_t kOffHdrAttachCount    = 0xF0; // attachment records
    constexpr size_t kOffHdrAttachPtr      = 0xF4; // -> attachment records (stride kAttachStride)
    constexpr size_t kAttachStride         = 0x28;
    constexpr size_t kOffAttachBone        = 0x04; // uint16: palette slot a child attached here rides
    constexpr size_t kOffAttachPos         = 0x08; // float[3]: offset applied to that slot
    // boneCombos M2Array {count, offset}: the per-section bone-index window the palette upload walks.
    // After the load walk the offset field holds a real pointer, hence the separate ...Ptr spelling.
    constexpr size_t kOffHdrBoneCombosCount = 0x78;
    constexpr size_t kOffHdrBoneCombosPtr   = 0x7C; // -> uint16 array
    constexpr size_t kOffHdrBoneIdxLutCount= 0xF8; // count of bone-index-by-id LUT entries
    constexpr size_t kOffHdrBoneIdxLutPtr  = 0xFC; // -> bone-index-by-id LUT (uint16 array, indexed by key_bone_id)

    // --- bone record fields (in the header bone array) ---
    constexpr size_t   kBoneStride        = 0x58;
    constexpr size_t   kOffBoneKeyId      = 0x00; // key_bone_id: canonical slot id (negative = none)
    constexpr size_t   kOffBoneFlags      = 0x04; // bone flags
    constexpr size_t   kOffBoneParent     = 0x08; // int16 parent index (0xFFFF = root)
    constexpr size_t   kOffBoneNameCrc    = 0x0C; // CRC32 of the bone name string (for name-based remap)
    constexpr size_t   kOffBoneTransTrack = 0x10; // translation track head (kOffTrackTimestampsCount = outer slots)
    constexpr size_t   kOffBoneRotTrack   = 0x24; // rotation track head
    constexpr size_t   kOffBoneScaleTrack = 0x38; // scale track head
    constexpr size_t   kOffBonePivot      = 0x4C; // pivot (bone origin in bind space)
    constexpr uint32_t kBoneBillboardMask = 0x78; // spherical + cylindrical-lock bits
    // Effective bone flags = runtime override mask OR'd with the static flags. Masks the build tests:
    constexpr uint32_t kBoneIgnoreParentMask = 0x07;  // rebuild against the instance root instead of the parent
    constexpr uint32_t kBoneAnimatedMask     = 0x280; // bone carries its own local transform
    constexpr uint32_t kBoneProceduralFlag   = 0x80;  // multiplies in the externally supplied matrix

    // --- runtime bone-state block (per bone, stride kRuntimeBoneStride) ---
    // The persisted sampling records the recomposition walk reads instead of re-sampling tracks;
    // the anchor/sequence fields feed the cadence eligibility scan and its mutation fingerprint.
    constexpr size_t kRuntimeBoneStride     = 0xAC;
    constexpr size_t kOffRtBoneTransValue   = 0x08; // float[3]: last sampled translation
    constexpr size_t kOffRtBoneQuatValue    = 0x1C; // float[4]: last sampled rotation quaternion
    constexpr size_t kOffRtBoneScaleValue   = 0x34; // float[3]: last sampled scale
    constexpr size_t kOffRtBoneAssignedSeq  = 0x48; // uint16: assigned sequence (0xFFFF = inherit)
    constexpr size_t kOffRtBoneSeqStart     = 0x4C; // int32: sequence start anchor (absolute clock ms)
    constexpr size_t kOffRtBoneSeqEnd       = 0x50; // int32: sequence end (absolute clock ms)
    constexpr size_t kOffRtBoneTimeScale    = 0x54; // float: playback speed factor
    constexpr size_t kOffRtBoneTimeOffset   = 0x5C; // int32: offset into the sequence
    constexpr size_t kOffRtBoneBlendSeq     = 0x6C; // uint16: blend-from sequence (0xFFFF = no blend)
    constexpr size_t kOffRtBoneProcMatrix   = 0x88; // -> externally driven per-frame matrix (null = none)
    constexpr size_t kOffRtBoneFlagMask     = 0x8C; // uint32: runtime bone-flag override mask
    constexpr size_t kOffRtBonePendingSeq   = 0x90; // uint32: pending sequence bookkeeping (0xFFFFFFFF idle)
    // Confirmed 2026-08-19 (wxl-equip-extension WXL-38): SetBoneSequence's primary write path
    // (kSetBoneSequence's own `primary=1` branch) writes here alongside kOffRtBonePendingSeq in the
    // same instruction sequence -- `mov [edi+0x90], eax` (seqId) immediately followed by
    // `mov word ptr [edi+0x94], cx` (Param3, this project's own `prevSeqId`).
    constexpr size_t kOffRtBonePrevSeqId    = 0x94; // uint16: SetBoneSequence's own prevSeqId param, as last written
    constexpr size_t kOffRtBoneBlendWeight  = 0xA8; // float: current blend weight (0 = none)

    // --- CharModelObject fields ---
    constexpr size_t kOffCmoRace      = 0x18; // uint32 race id
    constexpr size_t kOffCmoGender    = 0x1C; // uint32 gender (0 = male, 1 = female)
    constexpr size_t kOffCmoSceneNode = 0x38; // -> SceneNode (the root scene node for this character)
    // Per-internal-model-slot cached item display id (uint32[0xC], slots 0..0xB), written by
    // CharModelSlotDispatch on every real equip/clear event: it stores *itemDataPtr (the display id
    // itself, not the raw pointer) at [this + kOffCmoSlotItemId + modelSlot*4]. Confirmed via
    // disassembly 2026-08-16. Reading this directly for a freshly-resolved, live cmo gives the
    // CURRENT persisted display id for that slot with no caching/staleness window at all -- the
    // native engine keeps it current on every real event regardless of any extension.
    constexpr size_t kOffCmoSlotItemId = 0x428;

    // --- SceneNode fields ---
    constexpr size_t kOffSceneNodeOwner = 0x28; // -> CharModelObject that owns this scene node

    // --- track object fields read by the evaluators ---
    constexpr size_t kOffTrackTimestampsCount = 0x04;
    constexpr size_t kOffTrackTimestampsPtr   = 0x08;
    constexpr size_t kOffTrackValuesCount     = 0x0C;
    constexpr size_t kOffTrackValuesPtr       = 0x10;
    constexpr size_t kTrackTimestampStride    = 0x04; // one timestamp = u32 ms
    constexpr size_t kTrackVec3Stride         = 0x0C; // one vec3 value = 3 floats
    constexpr size_t kTrackQuatStride         = 0x08; // one compressed quat = 4 int16
    // Runtime-bone field: the current animation index used to pick the per-animation inner slot.
    constexpr size_t kOffRuntimeBoneAnimIdx   = 0x44;

    // --- ribbon-emitter object fields ---
    constexpr size_t kOffRibbonLayerCount   = 0x118; // draw-loop bound
    constexpr size_t kOffRibbonTexHandlePtr = 0x12C; // -> per-layer texture-handle array (stride 4)

    // --- typed views over the objects above ---
    // The constants are the curated landmarks; these structs give named, typed access to the same fields,
    // with every member offset checked against a constant at compile time (a wrong padding fails the build).
    // Only documented fields are named; the gaps are explicit padding. Pointers are 4 bytes on the 32-bit client.
#pragma pack(push, 1)
    /**
     * @brief I/O record read by the per-sequence rebase: the loaded buffer base and its byte size.
     *
     * Pointer-valued fields are stored as uint32_t, not void*, everywhere except the LAST field of a
     * struct: with more than one such field, sizeof(void*) would drive the padding between them, and
     * this header is 32/64-bit-neutral (sizeof(uint32_t) is not).
     */
    struct IoRecord
    {
        uint8_t  _pad00[kOffRecordBuffer];
        uint32_t buffer;           // kOffRecordBuffer (loaded bytes base)
        uint32_t size;             // kOffRecordSize (byte count)
    };
    static_assert(offsetof(IoRecord, buffer) == kOffRecordBuffer, "IoRecord.buffer");
    static_assert(offsetof(IoRecord, size)   == kOffRecordSize,   "IoRecord.size");

    /** @brief Async load node: holds the I/O record pointer. */
    struct LoadNode
    {
        uint8_t  _pad00[kOffNodeRecord];
        void*    record;           // kOffNodeRecord -> IoRecord
    };
    static_assert(offsetof(LoadNode, record) == kOffNodeRecord, "LoadNode.record");

    /** @brief SetupBatchAlpha draw context: the current element, the instance, and the live material. */
    struct DrawContext
    {
        uint8_t  _pad00[kOffRenderCtxElement];
        void*    element;          // kOffRenderCtxElement -> current M2Element (see kOffElementAlpha/BatchIndex)
        uint8_t  _pad54[kOffDrawCtxInstance - (kOffRenderCtxElement + sizeof(void*))];
        void*    instance;         // kOffDrawCtxInstance -> M2Instance
        uint8_t  _pad64[kOffDrawCtxMaterial - (kOffDrawCtxInstance + sizeof(void*))];
        void*    material;         // kOffDrawCtxMaterial -> Material
    };
    static_assert(offsetof(DrawContext, element)   == kOffRenderCtxElement, "DrawContext.element");
    static_assert(offsetof(DrawContext, instance)  == kOffDrawCtxInstance,  "DrawContext.instance");
    static_assert(offsetof(DrawContext, material)  == kOffDrawCtxMaterial, "DrawContext.material");

    /** @brief Live material record: the blend mode the draw uses to pick the alpha-test reference. */
    struct Material
    {
        uint8_t  _pad00[kOffMaterialBlend];
        uint16_t blend;            // kOffMaterialBlend (1 = alpha key)
    };
    static_assert(offsetof(Material, blend) == kOffMaterialBlend, "Material.blend");

    /**
     * @brief Runtime instance (= render context wrapper returned by GetRenderCtx).
     *        Both the character's scene node (cmo+0x38) and collection M2 render contexts
     *        share this layout. Covers the fields the per-frame palette build and its cadence
     *        logic touch: flags, links, anchors, the bone-state/palette pointers, the inline
     *        placement/root matrices and the staged speed/scale/translation block.
     *
     * Pointer-valued fields are stored as uint32_t, not void*, everywhere except the LAST field of a
     * struct: with more than one such field, sizeof(void*) would drive the padding between them, and
     * this header is 32/64-bit-neutral (sizeof(uint32_t) is not). A pad between two fixed-width fields
     * is only ever added when it is non-zero: MSVC's C2229 rejects a zero-length array as a non-trailing
     * member, so two adjacent fixed-width fields are left with no pad between them and rely on the
     * static_assert below to catch a future offset change instead.
     */
    struct M2Instance
    {
        uint8_t  _pad00[kOffInstOwnerFlags];
        uint32_t ownerFlags;       // kOffInstOwnerFlags (bit 0x20 = created via the map's doodad-
                                    // placement creation family -- exclusive to ADT/WMO-placed static
                                    // doodads, confirmed across all 50 scene model-creation call sites
                                    // in the client; never set by unit/mount/missile/spell-visual/UI
                                    // creation paths)
        uint8_t  _pad04[kOffInstInitFlags - (kOffInstOwnerFlags + sizeof(uint32_t))];
        uint32_t initFlags;        // kOffInstInitFlags (kInstFlag* bits)
        uint8_t  _pad14[kOffInstScene - (kOffInstInitFlags + sizeof(uint32_t))];
        uint32_t scene;            // kOffInstScene -> owning scene
        uint32_t model;            // kOffInstModel -> M2Model (shared model object)
        uint8_t  _pad30[kOffInstCmdRingHead - (kOffInstModel + sizeof(uint32_t))];
        uint32_t cmdRingHead;      // kOffInstCmdRingHead (== cmdRingTail when no deferred commands)
        uint32_t cmdRingTail;      // kOffInstCmdRingTail
        uint32_t lastAnimFrame;    // kOffInstLastAnimFrame (0 = never posed)
        uint8_t  _pad40[kOffInstParent - (kOffInstLastAnimFrame + sizeof(uint32_t))];
        uint32_t parent;           // kOffInstParent -> native parent M2 instance
        uint32_t attachEnable;     // kOffInstAttachEnable -> enable records (stride 0xC, u8 at +0x8)
        uint8_t  _pad50[kOffInstAttachSlot - (kOffInstAttachEnable + sizeof(uint32_t))];
        uint32_t attachSlot;       // kOffInstAttachSlot (0xFFFF = not slot-attached)
        uint32_t attachedHead;     // kOffInstAttachedHead -> first attached child
        uint8_t  _pad5c[kOffInstAttachedNext - (kOffInstAttachedHead + sizeof(uint32_t))];
        uint32_t attachedNext;     // kOffInstAttachedNext -> next sibling under the same parent
        uint32_t freezeAnchor;     // kOffInstFreezeAnchor (nonzero = externally frozen pose)
        uint8_t  _pad68[kOffInstViewDistSq - (kOffInstFreezeAnchor + sizeof(uint32_t))];
        float    viewDistSq;       // kOffInstViewDistSq
        uint8_t  _pad8c[kOffInstConstTrackGate - (kOffInstViewDistSq + sizeof(float))];
        uint32_t constTrackGate;   // kOffInstConstTrackGate
        uint32_t boneStates;       // kOffInstBoneStates -> RuntimeBone[boneCount]
        uint32_t bonePalettePtr;   // kOffInstBonePalette -> heap bone-matrix buffer (row-major 4x4, kBonePaletteStride each)
        uint8_t  _pad9c[kOffInstPlacement - (kOffInstBonePalette + sizeof(uint32_t))];
        float    placement[16];    // kOffInstPlacement (inline model->world matrix)
        float    viewRoot[16];     // kOffInstViewRoot (inline placement * view matrix)
        uint8_t  _pad134[kOffInstTexBinding - (kOffInstViewRoot + 16 * sizeof(float))];
        uint32_t texBinding;       // kOffInstTexBinding (mirrored from the parent each build)
        float    speedBase;        // kOffInstSpeedBase
        float    alphaBase;        // kOffInstAlphaBase
        float    scaleBase[3];     // kOffInstScaleBase
        float    transBase[3];     // kOffInstTransBase
        float    speedStage;       // kOffInstSpeedStage
        float    alphaStage;       // kOffInstAlphaStage
        float    scaleStage[3];    // kOffInstScaleStage
        float    transStage[3];    // kOffInstTransStage
    };
    static_assert(offsetof(M2Instance, ownerFlags)    == kOffInstOwnerFlags,    "M2Instance.ownerFlags");
    static_assert(offsetof(M2Instance, initFlags)     == kOffInstInitFlags,     "M2Instance.initFlags");
    static_assert(offsetof(M2Instance, scene)         == kOffInstScene,         "M2Instance.scene");
    static_assert(offsetof(M2Instance, model)         == kOffInstModel,         "M2Instance.model");
    static_assert(offsetof(M2Instance, cmdRingHead)   == kOffInstCmdRingHead,   "M2Instance.cmdRingHead");
    static_assert(offsetof(M2Instance, cmdRingTail)   == kOffInstCmdRingTail,   "M2Instance.cmdRingTail");
    static_assert(offsetof(M2Instance, lastAnimFrame) == kOffInstLastAnimFrame, "M2Instance.lastAnimFrame");
    static_assert(offsetof(M2Instance, parent)        == kOffInstParent,        "M2Instance.parent");
    static_assert(offsetof(M2Instance, attachEnable)  == kOffInstAttachEnable,  "M2Instance.attachEnable");
    static_assert(offsetof(M2Instance, attachSlot)    == kOffInstAttachSlot,    "M2Instance.attachSlot");
    static_assert(offsetof(M2Instance, attachedHead)  == kOffInstAttachedHead,  "M2Instance.attachedHead");
    static_assert(offsetof(M2Instance, attachedNext)  == kOffInstAttachedNext,  "M2Instance.attachedNext");
    static_assert(offsetof(M2Instance, freezeAnchor)  == kOffInstFreezeAnchor,  "M2Instance.freezeAnchor");
    static_assert(offsetof(M2Instance, viewDistSq)    == kOffInstViewDistSq,    "M2Instance.viewDistSq");
    static_assert(offsetof(M2Instance, constTrackGate)== kOffInstConstTrackGate,"M2Instance.constTrackGate");
    static_assert(offsetof(M2Instance, boneStates)    == kOffInstBoneStates,    "M2Instance.boneStates");
    static_assert(offsetof(M2Instance, bonePalettePtr)== kOffInstBonePalette,   "M2Instance.bonePalettePtr");
    static_assert(offsetof(M2Instance, placement)     == kOffInstPlacement,     "M2Instance.placement");
    static_assert(offsetof(M2Instance, viewRoot)      == kOffInstViewRoot,      "M2Instance.viewRoot");
    static_assert(offsetof(M2Instance, texBinding)    == kOffInstTexBinding,    "M2Instance.texBinding");
    static_assert(offsetof(M2Instance, speedBase)     == kOffInstSpeedBase,     "M2Instance.speedBase");
    static_assert(offsetof(M2Instance, alphaBase)     == kOffInstAlphaBase,     "M2Instance.alphaBase");
    static_assert(offsetof(M2Instance, scaleBase)     == kOffInstScaleBase,     "M2Instance.scaleBase");
    static_assert(offsetof(M2Instance, transBase)     == kOffInstTransBase,     "M2Instance.transBase");
    static_assert(offsetof(M2Instance, speedStage)    == kOffInstSpeedStage,    "M2Instance.speedStage");
    static_assert(offsetof(M2Instance, alphaStage)    == kOffInstAlphaStage,    "M2Instance.alphaStage");
    static_assert(offsetof(M2Instance, scaleStage)    == kOffInstScaleStage,    "M2Instance.scaleStage");
    static_assert(offsetof(M2Instance, transStage)    == kOffInstTransStage,    "M2Instance.transStage");

    /** @brief Scene clock/frame block read by the per-frame build and its cadence decisions. */
    struct M2SceneClock
    {
        uint8_t  _pad00[kOffSceneClock];
        uint32_t clock;            // kOffSceneClock (absolute animation clock, ms)
        uint32_t delta;            // this frame's clock delta
        uint32_t frame;            // kOffSceneFrame
    };
    static_assert(offsetof(M2SceneClock, clock) == kOffSceneClock, "M2SceneClock.clock");
    static_assert(offsetof(M2SceneClock, frame) == kOffSceneFrame, "M2SceneClock.frame");

    /** @brief Runtime model: flags, path stem, the parsed .m2 buffer, its size, and the live skin profile. */
    struct M2Model
    {
        uint8_t  _pad00[kOffModelFlags];
        uint32_t flags;            // kOffModelFlags (bit 2 selects the sibling-file open flag)
        uint8_t  _pad0c[kOffModelPathStem - (kOffModelFlags + sizeof(uint32_t))];
        char     pathStem[kOffModelHeader - kOffModelPathStem]; // kOffModelPathStem (inline path stem, no extension)
        void*    header;           // kOffModelHeader -> raw .m2 file buffer (parsed in place)
        uint8_t  _pad154[kOffModelFileSize - (kOffModelHeader + sizeof(void*))];
        uint32_t fileSize;         // kOffModelFileSize (byte size of the buffer at kOffModelHeader)
        void*    skin;             // kOffModelSkin -> live parsed skin profile
    };
    static_assert(offsetof(M2Model, flags)    == kOffModelFlags,    "M2Model.flags");
    static_assert(offsetof(M2Model, pathStem) == kOffModelPathStem, "M2Model.pathStem");
    static_assert(offsetof(M2Model, header)   == kOffModelHeader,   "M2Model.header");
    static_assert(offsetof(M2Model, fileSize) == kOffModelFileSize, "M2Model.fileSize");
    static_assert(offsetof(M2Model, skin)     == kOffModelSkin,     "M2Model.skin");

    /** @brief Parsed file header: the global flags, sequence/bone/attachment arrays, and the
     *         bone-index-by-id LUT. */
    struct M2FileHeader
    {
        uint8_t  _pad00[kOffHdrGlobalFlags];
        uint32_t globalFlags;       // kOffHdrGlobalFlags (bit 0x20 = physics; kHdrFlagFixedStaging)
        uint8_t  _pad14[kOffHdrSeqCount - (kOffHdrGlobalFlags + sizeof(uint32_t))];
        uint32_t seqCount;          // kOffHdrSeqCount
        void*    seqPtr;            // kOffHdrSeqPtr -> M2SequenceRec records (stride kSeqStride)
        uint8_t  _pad24[kOffHdrBoneCount - (kOffHdrSeqPtr + sizeof(void*))];
        uint32_t boneCount;         // kOffHdrBoneCount
        void*    boneArray;         // kOffHdrBoneArray -> M2Bone records (post-fixup data ptr)
        uint32_t keyBoneCount;      // kOffHdrKeyBoneCount
        void*    keyBoneArray;      // kOffHdrKeyBoneArray -> int16 array, -1 = no such key bone
        uint8_t  _pad3c[kOffHdrAttachCount - (kOffHdrKeyBoneArray + sizeof(void*))];
        uint32_t attachCount;       // kOffHdrAttachCount
        uint32_t attachPtr;         // kOffHdrAttachPtr -> M2Attachment records
        uint32_t boneIdxLutCount;   // kOffHdrBoneIdxLutCount (number of entries in the LUT)
        void*    boneIdxLutPtr;     // kOffHdrBoneIdxLutPtr -> uint16 array indexed by key_bone_id
    };
    static_assert(offsetof(M2FileHeader, globalFlags)    == kOffHdrGlobalFlags,     "M2FileHeader.globalFlags");
    static_assert(offsetof(M2FileHeader, seqCount)       == kOffHdrSeqCount,        "M2FileHeader.seqCount");
    static_assert(offsetof(M2FileHeader, seqPtr)         == kOffHdrSeqPtr,          "M2FileHeader.seqPtr");
    static_assert(offsetof(M2FileHeader, boneCount)      == kOffHdrBoneCount,       "M2FileHeader.boneCount");
    static_assert(offsetof(M2FileHeader, boneArray)      == kOffHdrBoneArray,       "M2FileHeader.boneArray");
    static_assert(offsetof(M2FileHeader, keyBoneCount)   == kOffHdrKeyBoneCount,    "M2FileHeader.keyBoneCount");
    static_assert(offsetof(M2FileHeader, keyBoneArray)   == kOffHdrKeyBoneArray,    "M2FileHeader.keyBoneArray");
    static_assert(offsetof(M2FileHeader, attachCount)    == kOffHdrAttachCount,     "M2FileHeader.attachCount");
    static_assert(offsetof(M2FileHeader, attachPtr)      == kOffHdrAttachPtr,       "M2FileHeader.attachPtr");
    static_assert(offsetof(M2FileHeader, boneIdxLutCount)== kOffHdrBoneIdxLutCount, "M2FileHeader.boneIdxLutCount");
    static_assert(offsetof(M2FileHeader, boneIdxLutPtr)  == kOffHdrBoneIdxLutPtr,   "M2FileHeader.boneIdxLutPtr");

    /**
     * @brief In-model track head: interpolation, global-sequence link, and the per-sequence outer
     *        arrays (timestamps + values). outerCount == 0 means the track carries no data at all.
     *
     * outerPtr is stored as uint32_t, not void*: it is not the last field, and a real pointer there
     * would let sizeof(void*) drive valuesOuterCount/valuesOuterPtr's offsets, breaking the fixed 0x14
     * size every M2Bone/M2Attachment track-head member below is checked against.
     */
    struct M2TrackHead
    {
        uint16_t interp;           // interpolation type
        uint16_t globalSeq;        // global-sequence index (0xFFFF = sequence-driven)
        uint32_t outerCount;       // kOffTrackTimestampsCount relative to the head
        uint32_t outerPtr;         // kOffTrackTimestampsPtr
        uint32_t valuesOuterCount; // kOffTrackValuesCount
        void*    valuesOuterPtr;   // kOffTrackValuesPtr
    };
    static_assert(sizeof(M2TrackHead) == 0x14, "M2TrackHead size");

    /** @brief Bone record in the header bone array (stride kBoneStride). */
    struct M2Bone
    {
        int32_t     keyBoneId;     // kOffBoneKeyId (canonical slot id; negative = no key bone)
        uint32_t    flags;         // kOffBoneFlags
        int16_t     parent;        // kOffBoneParent (0xFFFF = root)
        uint8_t     _pad0a[kOffBoneNameCrc - (kOffBoneParent + sizeof(int16_t))];
        uint32_t    nameCrc;       // kOffBoneNameCrc (CRC32 of the bone name, for name-based remap)
        M2TrackHead transTrack;    // kOffBoneTransTrack
        M2TrackHead rotTrack;      // kOffBoneRotTrack
        M2TrackHead scaleTrack;    // kOffBoneScaleTrack
        float       pivot[3];      // kOffBonePivot (bone origin in bind space)
    };
    static_assert(offsetof(M2Bone, keyBoneId)  == kOffBoneKeyId,      "M2Bone.keyBoneId");
    static_assert(offsetof(M2Bone, flags)      == kOffBoneFlags,      "M2Bone.flags");
    static_assert(offsetof(M2Bone, parent)     == kOffBoneParent,     "M2Bone.parent");
    static_assert(offsetof(M2Bone, nameCrc)    == kOffBoneNameCrc,    "M2Bone.nameCrc");
    static_assert(offsetof(M2Bone, transTrack) == kOffBoneTransTrack, "M2Bone.transTrack");
    static_assert(offsetof(M2Bone, rotTrack)   == kOffBoneRotTrack,   "M2Bone.rotTrack");
    static_assert(offsetof(M2Bone, scaleTrack) == kOffBoneScaleTrack, "M2Bone.scaleTrack");
    static_assert(offsetof(M2Bone, pivot)      == kOffBonePivot,      "M2Bone.pivot");
    static_assert(sizeof(M2Bone) == kBoneStride, "M2Bone size");

    /** @brief Attachment record (stride kAttachStride): the palette slot and offset a slot-attached
     *         child instance rides, plus the enable track sampled during full builds. */
    struct M2Attachment
    {
        uint32_t    id;
        uint16_t    bone;          // kOffAttachBone
        uint16_t    _pad06;
        float       pos[3];        // kOffAttachPos
        M2TrackHead enableTrack;
    };
    static_assert(offsetof(M2Attachment, bone) == kOffAttachBone, "M2Attachment.bone");
    static_assert(offsetof(M2Attachment, pos)  == kOffAttachPos,  "M2Attachment.pos");
    static_assert(sizeof(M2Attachment) == kAttachStride, "M2Attachment size");

    /** @brief Sequence record (stride kSeqStride = 0x40). Every field's byte position is confirmed
     *         -- see the comment on kOffSeqId above for how. seqId/length/flags/bboxMin/bboxMax/
     *         bboxRadius/nextAnimation were independently re-derived from this project's own
     *         disassembly; subId/frequency/rangeMin/rangeMax/blendTime/aliasNext carry the
     *         community-documented field names for their confirmed positions. */
    struct M2SequenceRec
    {
        uint16_t seqId;            // kOffSeqId -- the value SetBoneSequence/lookup functions match against
        uint16_t subId;            // kOffSeqSubId
        uint32_t length;           // kOffSeqLength, milliseconds
        float    movingSpeed;      // kOffSeqMovingSpeed
        uint32_t flags;            // kOffSeqFlags (bit 0x40 = alias -- see aliasNext)
        int16_t  frequency;        // kOffSeqFrequency
        uint16_t _pad12;
        uint32_t rangeMin;         // kOffSeqRangeMin
        uint32_t rangeMax;         // kOffSeqRangeMax
        uint32_t blendTime;        // kOffSeqBlendTime
        float    bboxMin[3];       // kOffSeqBBoxMin
        float    bboxMax[3];       // kOffSeqBBoxMax
        float    bboxRadius;       // kOffSeqBBoxRadius
        int16_t  nextAnimation;    // kOffSeqNextAnimation: variation-chain index, -1 = none
        uint16_t aliasNext;        // kOffSeqAliasNext: alias-chain target when flags bit 0x40 is set
    };
    static_assert(offsetof(M2SequenceRec, seqId)         == kOffSeqId,            "M2SequenceRec.seqId");
    static_assert(offsetof(M2SequenceRec, subId)         == kOffSeqSubId,         "M2SequenceRec.subId");
    static_assert(offsetof(M2SequenceRec, length)        == kOffSeqLength,        "M2SequenceRec.length");
    static_assert(offsetof(M2SequenceRec, movingSpeed)   == kOffSeqMovingSpeed,   "M2SequenceRec.movingSpeed");
    static_assert(offsetof(M2SequenceRec, flags)         == kOffSeqFlags,         "M2SequenceRec.flags");
    static_assert(offsetof(M2SequenceRec, frequency)     == kOffSeqFrequency,     "M2SequenceRec.frequency");
    static_assert(offsetof(M2SequenceRec, rangeMin)      == kOffSeqRangeMin,      "M2SequenceRec.rangeMin");
    static_assert(offsetof(M2SequenceRec, rangeMax)      == kOffSeqRangeMax,      "M2SequenceRec.rangeMax");
    static_assert(offsetof(M2SequenceRec, blendTime)     == kOffSeqBlendTime,     "M2SequenceRec.blendTime");
    static_assert(offsetof(M2SequenceRec, bboxMin)       == kOffSeqBBoxMin,       "M2SequenceRec.bboxMin");
    static_assert(offsetof(M2SequenceRec, bboxMax)       == kOffSeqBBoxMax,       "M2SequenceRec.bboxMax");
    static_assert(offsetof(M2SequenceRec, bboxRadius)    == kOffSeqBBoxRadius,    "M2SequenceRec.bboxRadius");
    static_assert(offsetof(M2SequenceRec, nextAnimation) == kOffSeqNextAnimation, "M2SequenceRec.nextAnimation");
    static_assert(offsetof(M2SequenceRec, aliasNext)     == kOffSeqAliasNext,     "M2SequenceRec.aliasNext");
    static_assert(sizeof(M2SequenceRec) == kSeqStride, "M2SequenceRec size");

    /** @brief Track object read by the evaluators: the timestamp and value sub-arrays (count + ptr each). */
    struct M2Track
    {
        uint8_t   _pad00[kOffTrackTimestampsCount];
        uint32_t  timestampsCount; // kOffTrackTimestampsCount
        uint32_t  timestampsPtr;   // kOffTrackTimestampsPtr
        uint32_t  valuesCount;     // kOffTrackValuesCount
        void*     valuesPtr;       // kOffTrackValuesPtr
    };
    static_assert(offsetof(M2Track, timestampsCount) == kOffTrackTimestampsCount, "M2Track.timestampsCount");
    static_assert(offsetof(M2Track, timestampsPtr)   == kOffTrackTimestampsPtr,   "M2Track.timestampsPtr");
    static_assert(offsetof(M2Track, valuesCount)     == kOffTrackValuesCount,     "M2Track.valuesCount");
    static_assert(offsetof(M2Track, valuesPtr)       == kOffTrackValuesPtr,       "M2Track.valuesPtr");

    /** @brief Runtime bone state (stride kRuntimeBoneStride): the persisted sampling records the
     *         recomposition walk consumes, and the sequence/blend anchors the cadence scan reads. */
    struct RuntimeBone
    {
        uint32_t transHint[2];     // key-search hints (channel A/B)
        float    transValue[3];    // kOffRtBoneTransValue: last sampled translation
        uint32_t rotHint[2];
        float    quatValue[4];     // kOffRtBoneQuatValue: last sampled rotation
        uint32_t scaleHint[2];
        float    scaleValue[3];    // kOffRtBoneScaleValue: last sampled scale
        uint32_t time;             // current channel-A time within the sequence (ms)
        uint16_t animIndex;        // kOffRuntimeBoneAnimIdx: per-animation inner-slot selector
        uint16_t boneIndex;
        uint16_t assignedSeq;      // kOffRtBoneAssignedSeq (0xFFFF = inherit from the parent bone)
        uint8_t  clampDone;
        uint8_t  callbackFlag;
        int32_t  seqStart;         // kOffRtBoneSeqStart (absolute clock ms)
        int32_t  seqEnd;           // kOffRtBoneSeqEnd (absolute clock ms)
        float    timeScale;        // kOffRtBoneTimeScale
        float    invSpeed;
        int32_t  timeOffset;       // kOffRtBoneTimeOffset
        int32_t  repeatCount;
        uint32_t timeB;            // channel-B (blend-from) mirror of +0x40..+0x5C
        uint16_t animIndexB;
        uint8_t  _pad6a[kOffRtBoneBlendSeq - 0x6A];
        uint16_t blendSeq;         // kOffRtBoneBlendSeq (0xFFFF = no blend running)
        uint8_t  _pad6e[kOffRtBoneProcMatrix - (kOffRtBoneBlendSeq + sizeof(uint16_t))];
        uint32_t procMatrix;       // kOffRtBoneProcMatrix (-> externally driven 4x4; 0 = none)
        uint32_t flagMask;         // kOffRtBoneFlagMask (OR'd into the static bone flags)
        uint32_t pendingSeq;       // kOffRtBonePendingSeq (0xFFFFFFFF idle)
        uint16_t prevSeqId;        // kOffRtBonePrevSeqId
        uint8_t  _pad96[kOffRtBoneBlendWeight - (kOffRtBonePrevSeqId + sizeof(uint16_t))];
        float    blendWeight;      // kOffRtBoneBlendWeight (0 = no blend contribution)
    };
    static_assert(offsetof(RuntimeBone, transValue)  == kOffRtBoneTransValue,  "RuntimeBone.transValue");
    static_assert(offsetof(RuntimeBone, quatValue)   == kOffRtBoneQuatValue,   "RuntimeBone.quatValue");
    static_assert(offsetof(RuntimeBone, scaleValue)  == kOffRtBoneScaleValue,  "RuntimeBone.scaleValue");
    static_assert(offsetof(RuntimeBone, animIndex)   == kOffRuntimeBoneAnimIdx,"RuntimeBone.animIndex");
    static_assert(offsetof(RuntimeBone, assignedSeq) == kOffRtBoneAssignedSeq, "RuntimeBone.assignedSeq");
    static_assert(offsetof(RuntimeBone, seqStart)    == kOffRtBoneSeqStart,    "RuntimeBone.seqStart");
    static_assert(offsetof(RuntimeBone, seqEnd)      == kOffRtBoneSeqEnd,      "RuntimeBone.seqEnd");
    static_assert(offsetof(RuntimeBone, timeScale)   == kOffRtBoneTimeScale,   "RuntimeBone.timeScale");
    static_assert(offsetof(RuntimeBone, timeOffset)  == kOffRtBoneTimeOffset,  "RuntimeBone.timeOffset");
    static_assert(offsetof(RuntimeBone, blendSeq)    == kOffRtBoneBlendSeq,    "RuntimeBone.blendSeq");
    static_assert(offsetof(RuntimeBone, procMatrix)  == kOffRtBoneProcMatrix,  "RuntimeBone.procMatrix");
    static_assert(offsetof(RuntimeBone, flagMask)    == kOffRtBoneFlagMask,    "RuntimeBone.flagMask");
    static_assert(offsetof(RuntimeBone, pendingSeq)  == kOffRtBonePendingSeq,  "RuntimeBone.pendingSeq");
    static_assert(offsetof(RuntimeBone, prevSeqId)   == kOffRtBonePrevSeqId,   "RuntimeBone.prevSeqId");
    static_assert(offsetof(RuntimeBone, blendWeight) == kOffRtBoneBlendWeight, "RuntimeBone.blendWeight");
    static_assert(sizeof(RuntimeBone) == kRuntimeBoneStride, "RuntimeBone size");

    /** @brief Ribbon emitter: the draw-loop layer count and the per-layer texture-handle array pointer. */
    struct RibbonEmitter
    {
        uint8_t  _pad00[kOffRibbonLayerCount];
        uint32_t layerCount;       // kOffRibbonLayerCount (draw-loop bound)
        uint8_t  _pad11c[kOffRibbonTexHandlePtr - (kOffRibbonLayerCount + sizeof(uint32_t))];
        void**   texHandles;       // kOffRibbonTexHandlePtr -> per-layer texture-handle array (stride 4)
    };
    static_assert(offsetof(RibbonEmitter, layerCount) == kOffRibbonLayerCount,   "RibbonEmitter.layerCount");
    static_assert(offsetof(RibbonEmitter, texHandles) == kOffRibbonTexHandlePtr, "RibbonEmitter.texHandles");

    /** @brief Character model object: race/gender ids and the root scene node pointer. */
    struct CharModelObject
    {
        uint8_t  _pad00[kOffCmoRace];
        uint32_t raceId;           // kOffCmoRace
        uint32_t genderId;         // kOffCmoGender (0 = male, 1 = female)
        uint8_t  _pad20[kOffCmoSceneNode - (kOffCmoGender + sizeof(uint32_t))];
        void*    sceneNode;        // kOffCmoSceneNode -> SceneNode
    };
    static_assert(offsetof(CharModelObject, raceId)    == kOffCmoRace,      "CharModelObject.raceId");
    static_assert(offsetof(CharModelObject, genderId)  == kOffCmoGender,    "CharModelObject.genderId");
    static_assert(offsetof(CharModelObject, sceneNode) == kOffCmoSceneNode, "CharModelObject.sceneNode");

    /** @brief Scene node: the CharModelObject that owns this node. */
    struct SceneNode
    {
        uint8_t  _pad00[kOffSceneNodeOwner];
        void*    owner;            // kOffSceneNodeOwner -> CharModelObject
    };
    static_assert(offsetof(SceneNode, owner) == kOffSceneNodeOwner, "SceneNode.owner");
#pragma pack(pop)

    // --- signatures ---
    // Model init / skin finalize: native this-in-ECX; declared with a dummy second parameter so the
    // trampoline routes the model into the this-register.
    using M2_InitFn         = int(__fastcall*)(void* model);
    using M2_FinalizeSkinFn = void(__fastcall*)(void* model);
    // Anim read-completion callback (node on stack).
    using M2_AnimLoadCompleteFn = void(__cdecl*)(void* node);
    // Per-batch alpha setter: native this-in-ECX.
    using M2_SetupBatchAlphaFn = void(__fastcall*)(void* drawContext);
    using M2_SortOpaqueGeoBatchesFn = int(__cdecl*)(void* lhs, void* rhs);
    // Alpha-test reference push (ref on stack).
    using M2_PushAlphaRefFn = void(__cdecl*)(float ref);
    // Ribbon de-relocator (base, fileSize, ctx, ribbons): rebases each ribbon's sub-array offsets.
    using M2_RibbonDeRelocateFn = int(__cdecl*)(int base, unsigned int fileSize, int ctx, unsigned int* ribbons);
    // Ribbon emitter draw: native this-in-ECX.
    using M2_RibbonDrawFn = int(__fastcall*)(void* emitter, void* edx, void* stateBlock);
    // Texture-handle resolver (handle, ...).
    using M2_TexResolveFn = void*(__cdecl*)(void* handle, int a, int b);
    // Sampler bind: native this-in-ECX.
    using M2_SamplerBindFn = void(__fastcall*)(void* device, void* edx, uint32_t selector, void* tex);

    // --- attachment / resource signatures ---
    // CreateSceneModel(scene, edx, path, 0): ret 8 (2 stack args: path + trailing zero).
    using M2_CreateSceneModelFn = void*(__fastcall*)(void* scene, void* edx, void* path, uint32_t zero);
    // Deprecated spelling, kept so no published name disappears.
    using M2_GetRenderCtxFn     = M2_CreateSceneModelFn;
    // AttachToScene(renderCtx, edx, subObj, slot, 0, 0): ret 16 (4 stack args: subObj, slot, 0, 0).
    using M2_AttachToSceneFn    = void (__fastcall*)(void* renderCtx, void* edx, void* subObj, uint32_t slot, uint32_t zero1, uint32_t zero2);
    // DetachSlot(subObj, edx, slot): detaches the M2 from a scene slot, releasing its render ctx.
    using M2_DetachSlotFn       = void (__fastcall*)(void* subObj, void* edx, uint32_t slot);
    // ReleaseRenderCtx(renderCtx, edx): releases a render context.
    using M2_ReleaseRenderCtxFn = void (__fastcall*)(void* renderCtx, void* edx);
    // BindTexSlot(renderCtx, edx, key, modelPtr): ret 8 (key=2, then modelPtr on stack).
    using M2_BindTexSlotFn      = void (__fastcall*)(void* renderCtx, void* edx, uint32_t key, void* modelPtr);
    // LoadResource(path, flags, statusOut, flags2): same call shape as Gx::TextureCreate.
    using M2_LoadResourceFn     = void*(__cdecl*)(const char* path, uint32_t flags, int* statusOut, uint32_t flags2);
    // ReleaseResource(resource): releases a resource handle returned by LoadResource.
    using M2_ReleaseResourceFn  = void (__cdecl*)(void* resource);

    // --- per-frame / slot hook signatures ---
    // PerFrameUpdate(renderCtx, edx): per-render-ctx per-frame scene-graph update.
    using M2_PerFrameUpdateFn   = void (__fastcall*)(void* renderCtx, void* edx);
    // BuildBonePalette(instance, edx, sa1, sa2, sa3, sa4, sa5): fills the instance bone palette
    // from the current animation pose. fastcall, 5 stack args, ret 0x14. Hook POST-order to
    // override the engine's fill (e.g. CharSweep for collection M2s attached to a character).
    using M2_BuildBonePaletteFn = void (__fastcall*)(void* renderCtx, void* edx,
        void* sa1, void* sa2, void* sa3, uint32_t sa4, uint32_t sa5);
    using M2_RenderBatchShadowMapFn = void (__fastcall*)(
        void* instance, void* edx, uint32_t batchMode, void* skinBatch, void* drawList,
        uint32_t drawIndex, void* skinSection, void* previousSection);
    // SlotDispatch(cmo, edx, modelSlot, itemDataPtr, postFlag): equip-slot handler; loads the model.
    using M2_SlotDispatchFn     = void (__fastcall*)(void* cmo, void* edx, uint32_t modelSlot, void* itemDataPtr, uint32_t postFlag);
    // SlotClear(cmo, edx, equipSlotWow): clears a WoW equipment slot on the CMO.
    using M2_SlotClearFn        = void (__fastcall*)(void* cmo, void* edx, uint32_t equipSlotWow);

    // character component: geosets, item slots, baked textures
    /// The gate that rejects character customization data, which must be widened for values beyond the
    /// stock ranges. __cdecl, caller-cleaned.
    constexpr uintptr_t kCharValidateComponentData         = 0x004E9D50;
    /// Resolve the base skin and face texture set for a character, where modern texture paths must be
    /// substituted. __thiscall, 6 stack args.
    ///
    /// Loads ONE section's source texture and installs it: it walks the variation table down to a
    /// section record, takes the path at kOffSectionTexturePaths + slot * 4, creates the texture,
    /// stores the handle at kOffCharComponentTextures + (slot + type * kSectionSlotCount) * 4 and
    /// raises the component's dirty bit for the region. Substituting a path is therefore the whole of
    /// what a reworked model needs here; the composition around it is already correct.
    constexpr uintptr_t kCharLoadBaseVariation             = 0x004EA1F0;
    /// Intercept the skin variation selection, the customization axis with the widest downstream
    /// effect. __thiscall, 4 stack args.
    constexpr uintptr_t kCharSetSkinColor                  = 0x004EA6B0;
    /// The shared routine that binds any visual to a character attachment point, one hook covering
    /// every equipped model. __cdecl, caller-cleaned.
    constexpr uintptr_t kCharAddAttachmentLink             = 0x004EAA70;
    /// Take over weapon and off-hand model placement, including its sheathe behaviour. __cdecl, caller-
    /// cleaned.
    constexpr uintptr_t kCharAddHandItem                   = 0x004EACD0;
    /// Control tabard composition and its colour channels, the most complex baked-texture path on a
    /// character. __thiscall, 5 stack args.
    constexpr uintptr_t kCharApplyGuildColor               = 0x004EC1C0;
    /// Own the geoset selection that turns equipment and customization choices into visible character
    /// geometry. __thiscall, caller-cleaned.
    constexpr uintptr_t kCharGeosetRenderPrep              = 0x004ED900;
    /// The whole player geoset decision, start to finish: blanket-hide, show the chosen ids, rebuild
    /// the draw list. It hides ids 0..2000 before re-showing anything, which is the entire reason a
    /// model numbered beyond that range cannot be made geoset-correct through it: what is never hidden
    /// stays visible, and the visibility array starts all-on. Replacing it for one model means the same
    /// three moves over a wider range and a different source of chosen ids.
    /// The receiver is the character component; the model instance reaches SetGeometryVisible in ECX.
    using Char_GeosetRenderPrepFn = void(__fastcall*)(void* component, void* edx);
    /// -> the model instance the geoset decision writes to. Reloaded from the component before every
    /// one of its SetGeometryVisible calls, so it is the owner of the visibility array and the
    /// receiver any replacement decision has to pass in turn.
    constexpr size_t kOffCharComponentInstance             = 0x38;
    /// uint32[19]: the chosen geoset id per customization slot, filled by the CCharacterComponent
    /// setters. This array IS the stock client's customization vocabulary; slot 0x11 is substituted
    /// with 1703 when the component's own eye-glow condition holds.
    constexpr size_t kOffCharComponentGeosetSlots          = 0x144;
    constexpr uint32_t kCharComponentGeosetSlotCount       = 19;
    /// Intercept unequip so extension-added visual slots are torn down with the stock ones. __thiscall,
    /// 1 stack arg.
    constexpr uintptr_t kCharRemoveItem                    = 0x004EE460;
    /// Allocate the composited character skin texture, where format and resolution are decided.
    /// __thiscall, caller-cleaned.
    constexpr uintptr_t kCharCreateBaseTexture             = 0x004EFF10;
    /// The component allocator behind character composition, hookable to enlarge or pool differently.
    /// __cdecl, caller-cleaned.
    constexpr uintptr_t kCharAllocComponent                = 0x004F0980;
    /// The per-frame entry that rebuilds a character's composited appearance, the right place to force
    /// or suppress a rebuild. __thiscall, 1 stack arg.
    constexpr uintptr_t kCharRenderPrep                    = 0x004F1520;
    /// The matching free, needed to keep an extension's component tracking exact. __cdecl, caller-
    /// cleaned.
    constexpr uintptr_t kCharFreeComponent                 = 0x004F16C0;

    // --- character skin composition: where the sheet's source textures are chosen ------------------
    //
    /// Accepts (table, race, sex, sectionType, variation, colour, outFound) and returns the section
    /// record, or null. sectionType is bounded to 0..4 and indexes [race][sex][type].
    constexpr uintptr_t kCharGetSectionsRecord             = 0x004F3BA0;
    /// The table those two index, one entry per (race, sex, sectionType). Built at load, so it reads
    /// as zeroes in the image and only means anything on a running client.
    constexpr uintptr_t kCharVariationArray                = 0x00B6B864;
    constexpr uintptr_t kCharVariationArrayLength          = 0x00B6B874;
    /// Turns a path into a texture handle. __cdecl, caller-cleaned. This ACQUIRES: it takes a
    /// reference whether it finds the entry or allocates one, so every call needs its release below.
    constexpr uintptr_t kCharCreateTextureFromPath         = 0x004F3930;
    /// Gives back one reference taken above, freeing the entry with the last of them. __cdecl,
    /// 1 stack arg.
    constexpr uintptr_t kTextureCacheRelease               = 0x004F31A0;
    /// Paints one region of the sheet from a source texture. Every region painter is a call to this
    /// with its own region index, and it returns without painting when the source carries no mip
    /// chain -- a test it makes before looking at the source in any other way, so the chain is where
    /// the pixels come from and not an optimisation.
    /// It reads the source at the region's OWN corner, so the source it expects covers the whole
    /// sheet and the region is a window onto it. It also clears the alpha-bit byte of its description
    /// copy before either paste, which forces the opaque blit whatever the source actually carries.
    /// The source level it asks for is found by halving the source width until it equals the sheet
    /// resolution, counting the steps -- a width that is not a power-of-two multiple of that figure
    /// never reaches the equality. __cdecl: (regionIndex, source, destLevels).
    constexpr uintptr_t kCharPaintRegion                   = 0x004F07D0;
    /// The sibling copy, for a source authored AS the region: it reads from (0, 0) instead, and its
    /// level search halves the source width down to the region's width rather than the sheet's. It
    /// leaves the alpha-bit byte alone, so this is the one that can reach the masked, blend and
    /// indexed-alpha blits. __cdecl, same arguments.
    constexpr uintptr_t kCharPaintRegionFromOrigin         = 0x004F08A0;
    /// The mip-chain test that gate rests on. __cdecl, 1 stack arg.
    constexpr uintptr_t kTextureCacheHasMips               = 0x004F2D80;
    /// Copies one block of a source texture into the sheet, MAGNIFYING it by two: the source is read
    /// at half the destination's scale and doubled, averaging across each seam. Level 0 of the sheet
    /// is filled that way and the levels below it one-for-one, which is why the destination is an
    /// array of level pointers and not a single surface.
    /// __cdecl: (source, destLevels, destOrigin, sourceOrigin, size, description).
    constexpr uintptr_t kCharPaste                         = 0x004EF9D0;
    /// The same copy REDUCING instead, for a source authored above the sheet's own resolution: the
    /// extra argument is the source level to start from, and the destination level it lands on is
    /// counted from there. The painter picks between the two on that test alone, which sends the
    /// large body skin here and the smaller face and scalp sources to the magnifying copy above.
    /// __cdecl, same arguments plus that starting level.
    constexpr uintptr_t kCharPasteScale                    = 0x004EC550;
    /// The opaque blit the magnifying copy delegates to, and where that copy's palette test actually
    /// lives: it resolves the palette itself and RETURNS having written nothing when there is none.
    /// So the two copies fail differently on a source they cannot read -- an untouched region here,
    /// flat green from the fill below -- and a replacement has to cover both.
    /// It doubles source level 0 into destination level 0, then hands the rest of the chain to the
    /// one-for-one blit with every rectangle, the pitch and the source origin halved once, and the
    /// destination level index offset by one.
    /// __cdecl: (source, destLevels, destOrigin, destPitch, sourceOrigin, size, description).
    constexpr uintptr_t kCharPasteOpaque                   = 0x004E89F0;
    /// What the reducing copy calls instead when the source has no palette. It is not even handed the
    /// source: its whole body writes 0xFF00FF00 per pixel, an opaque GREEN, over every level of the
    /// region. A green body on screen is this, and it is why a source that is not palettised has to be
    /// intercepted rather than left to fail quietly.
    /// __cdecl: (destLevels, destOrigin, destPitch, size, description, firstLevel, -firstLevel).
    constexpr uintptr_t kCharPasteNoPalette                = 0x004E82D0;
    /// Byte 5 of the six-byte description both copies receive, which is kOffTexEntryAlphaBits seen
    /// through that description. It selects the blit, and only the four values below exist; anything
    /// else silently paints nothing.
    constexpr size_t kOffSourceDescMode = 5;
    constexpr uint8_t kSourceModeOpaque       = 0; ///< no alpha: the whole rectangle is overwritten
    /// One alpha bit per pixel, in a plane of its own after the index plane: the pixel is written
    /// opaque or it is left alone. Reducing, that is a test and nothing more; magnifying, the bit is
    /// expanded to 0 or 255, averaged across the seams like any other channel and then weighed.
    constexpr uint8_t kSourceModeMasked       = 1;
    /// Four alpha bits per pixel, two to a byte in that same trailing plane, low nibble first. Each
    /// nibble is expanded to `n | n << 4` before it weighs anything, which is why this mode and the
    /// one below share their arithmetic once a source's alpha is a byte.
    constexpr uint8_t kSourceModeBlend        = 4;
    /// One alpha byte per pixel in that trailing plane. Both weighted modes combine as
    /// `(source * a + destination * (255 - a)) >> 8` per channel -- a shift by eight over weights
    /// that run to 255, so an opaque source pixel lands one part in 256 short of itself -- and state
    /// the destination's own alpha as 255.
    constexpr uint8_t kSourceModeIndexedAlpha = 8;
    /// The source's palette, or null. Both copies resolve it BEFORE looking at anything else, and a
    /// source that has none is dropped there: the magnifying copy returns outright, the reducing one
    /// calls a fill that is not even handed the source. Neither ever reaches its format dispatch. So
    /// every source that is not palettised paints nothing at all, whatever its format. __cdecl.
    constexpr uintptr_t kTextureCacheGetPal                = 0x004F2D40;
    /// A source level's pixels, or null: the container's file image offset by that level's own entry.
    /// Bounds-checked against the level count below. __cdecl: (source, level).
    constexpr uintptr_t kTextureCacheGetLevel              = 0x004F2D00;
    /// The composited sheet's edge, in pixels. Its rows are that many 32-bit pixels wide, and both
    /// copies derive their destination pitch from it rather than being handed one. The allocation
    /// passes it as BOTH width and height, so the stock sheet can only ever be square -- which a
    /// modern texture layout is not.
    constexpr uintptr_t kCharSheetResolution               = 0x00B6B5FC;
    /// Fills the six-byte source description both copies read, from a texture handle, and is the ONLY
    /// way in: it starts the file's read when there is none, takes the cache's lock over the streaming
    /// path, and refuses while the entry has nothing to describe. Reaching past it to the raw load
    /// below leaves both the lock and that refusal out.
    /// __cdecl: (source, outDescription, wait). A zero wait is a PREDICATE and nothing more: on an
    /// install without a streaming manifest it does no work at all past starting the read, and what
    /// makes the read land is a handler on the same event queue as the composition -- so asked from
    /// inside a composition it can only ever answer "not yet". A non-zero wait pumps that handler on
    /// the calling thread, which is what the client's own region painters pass. Nesting one such wait
    /// inside another is fatal to the client.
    /// @return non-zero once the description is filled.
    constexpr uintptr_t kTextureDescribe                   = 0x004F2E50;
    /// Creates a texture of a stated size, as opposed to one loaded from a path. __cdecl, 9 stack
    /// args, caller-cleaned: (width, height, format, usage, flags, owner, filler, name, one). The
    /// name is a literal the caller supplies, which is what lets one creation be told from another.
    constexpr uintptr_t kTextureCreateSized                = 0x004B9200;
    /// The two section walks: they paint every dirty region of the composite and hand each painted
    /// rectangle to the card. Both are __fastcall with the component in ecx and no stack args, and
    /// both end a FULL rebuild -- kCharRebuildSheet set on the component -- by uploading
    /// (0, 0, resolution, resolution) and returning, skipping the per-region uploads entirely. That
    /// rectangle is the one square figure on both axes, so under a composite that is not square
    /// everything outside it is composed correctly in the CPU image and never reaches the sheet
    /// texture. The per-region uploads need no such correction: they read the live region table,
    /// which already holds whatever arrangement the composite is painted in.
    ///
    /// This one runs from the per-frame render prep.
    constexpr uintptr_t kCharPrepSections                  = 0x004EE0D0;
    /// The same walk for a composition request that has come back from the composition thread,
    /// reached from the per-frame component update -- so on the thread that owns the device, not on
    /// the composition thread. Its full-rebuild branch is the same square upload.
    constexpr uintptr_t kCharUpdateSections                = 0x004E9510;
    /// Resolves a texture handle to the card-side texture, and takes NO rectangle:
    /// __cdecl, 3 stack args, (handle, mode, callback), called as (handle, 1, 0) by everything that
    /// composes. The rectangle visible at the section walks' call sites belongs to the update below,
    /// whose arguments are pushed first and cleaned separately -- `add esp, 0x0C` for this call and
    /// `add esp, 0x18` for that one. A decompiler folds the two argument lists into this one.
    constexpr uintptr_t kTextureGetGxTex                   = 0x004B6CB0;
    /// Marks a rectangle of that texture for upload to the card. __cdecl, 6 stack args,
    /// caller-cleaned: (gxTex, left, top, right, bottom, immediate), stored as
    /// {top, left, bottom, right} and handed to the device. A dirty region is named as
    /// (x, y, x + w, y + h); a full rebuild as (0, 0, resolution, resolution).
    constexpr uintptr_t kGxTexUpdate                       = 0x00681F20;
    /// Texture cache entry. The six bytes from kOffTexEntryWidth are also what both copies receive as
    /// their "description" argument, laid out exactly as they are here.
    /// Opens the source's file and starts reading it. Creating the cache entry does NOT do this: the
    /// entry is a name until something asks for the bytes, and nothing does on its own. Raw, and
    /// unconditional -- it allocates a fresh read object over any already in flight, whose completion
    /// then lands on freed memory -- so ask through kTextureDescribe above, never here.
    /// __fastcall, the entry in ecx.
    constexpr uintptr_t kTextureCacheLoad                  = 0x004F2BE0;
    /// The read in flight. Note that the image pointer below is allocated BEFORE the bytes arrive, so
    /// a non-null image says nothing about readiness -- but neither does this field read on its own,
    /// since the thread completing the read clears it. kTextureDescribe is the answer, under its lock.
    constexpr size_t kOffTexEntryPendingRead = 0x18;
    constexpr size_t kOffTexEntryWidth      = 0x1C; ///< uint16, height at +0x1E
    constexpr size_t kOffTexEntryLevelCount = 0x20; ///< byte
    constexpr size_t kOffTexEntryAlphaBits  = 0x21; ///< byte: 0, 1, 4 or 8
    constexpr size_t kOffTexEntryImage      = 0xAC; ///< the container file, verbatim; null until the
                                                    ///< async load has landed
    constexpr size_t kOffTexEntryFlags      = 0xB0;
    /// Set on an entry whose image must not be read. Both the palette and the level lookup refuse on
    /// it before they touch the image at all.
    constexpr uint32_t kTexEntryUnreadable  = 0x00100000;
    /// Where each composition region lands on the sheet: {x, y, width, height} per region, built at
    /// load. This is the arrangement the composited sheet is painted in, and a reworked model's UVs
    /// are authored against a different one -- comparing the two is what says how they correspond.
    /// WHERE THE COMPOSITION ACTUALLY WRITES. Not the sheet texture: a CPU image allocated ONCE at
    /// component initialisation, square and at the resolution figure, that every region paints into
    /// and that the sheet is then updated from. Enlarging the texture alone changes nothing here, and
    /// composing at a wider pitch than this image was allocated for runs off the end of it.
    constexpr uintptr_t kCharComposeImage                  = 0x00B6B870;
    /// The same image again for the compressed and threaded composition paths, allocated beside it
    /// and only when those are enabled.
    constexpr uintptr_t kCharComposeImageCompressed        = 0x00B6B86C;
    constexpr uintptr_t kCharComposeImageThreaded          = 0x00B6B868;
    /// One-time character-component setup, and the ONLY place the three images above are allocated.
    /// It clamps its resolution argument to [6, 9] (and to 8 without compression), sets the sheet
    /// resolution figure to 1 << n, derives the region arrangement from the shipped one as
    /// `master >> (9 - n)`, then allocates the images square at that figure. Every allocation it
    /// reaches -- including the composition requests' own images, through the threaded setup it calls
    /// -- asks for exactly (figure, figure), which is what lets a different geometry be recognised
    /// there. Called once at CVar registration, so a geometry meant to change those images has to be
    /// set before that.
    /// __cdecl: (gxFormat, resolution, threaded, compressed).
    constexpr uintptr_t kCharComponentInitialize           = 0x004F1A20;
    /// Allocates one such image. __cdecl: (pixelFormat, width, height) -- two dimensions, unlike the
    /// figure the client fills them from.
    constexpr uintptr_t kTextureAllocMippedImg             = 0x004B7220;
    /// The pixel format the composition image is allocated with.
    constexpr uint32_t  kCharComposeFormat                 = 2;
    /// Whether the compressed image exists at all; the threaded one needs this and threading both.
    constexpr uintptr_t kCharComposeCompressionOn          = 0x00B6B4E4;

    /// The region arrangement the client SHIPS, for a 512 sheet. What the composition reads is
    /// derived from it at initialisation as `master >> (9 - log2(resolution))`, which is why the two
    /// tables sit next to each other and why the derived one is not authored anywhere.
    constexpr uintptr_t kCharRegionRectsMaster             = 0x00B6B928;
    constexpr uintptr_t kCharRegionRects                   = 0x00B6B888;
    constexpr size_t    kCharRegionRectStride              = 0x10;
    /// TEN, not eleven. The section walk states it twice independently -- once as a plain bound and
    /// once as the address the rectangle pointer must stay below -- and both stop after ten. The
    /// dirty mask has an eleventh bit set, which is what makes twelve look plausible from that side;
    /// nothing ever reads an eleventh rectangle. Immediately after the table is component setup data,
    /// so treating it as one entry longer writes over that instead.
    constexpr uint32_t  kCharRegionCount                   = 10;

    /// Section record: three texture paths and the flags the callers test.
    constexpr size_t kOffSectionTexturePaths = 0x10; ///< char*[3], one per composition slot
    constexpr size_t kOffSectionFlags        = 0x1C;
    constexpr uint32_t kSectionSlotCount     = 3;

    /// Component fields the load above writes.
    constexpr size_t kOffCharComponentRace    = 0x18;
    constexpr size_t kOffCharComponentSex     = 0x1C;
    /// Bitmask, one bit per region: the regions the composition still owes a repaint. The section
    /// walk tests it region by region and reaches a region's painter ONLY for a set bit, so a region
    /// whose bit is clear is not painted at all, whatever sources it holds. The walk's caller clears
    /// the whole mask the moment the walk returns, so the mask can only be read from inside one --
    /// after that it is zero and says nothing about the pass that just ran.
    constexpr size_t kOffCharComponentDirty   = 0x0C;
    /// int[]: the loaded texture handle per (slot + sectionType * kSectionSlotCount).
    /// The composited sheet itself, once created. Zero until then, and the allocation is skipped for
    /// a component that already has one -- which is what makes a sheet outlive the rebuild that built
    /// it and forbids changing its size anywhere but at creation.
    /// Work the component owes, one bit per kind. The per-frame entry answers every other call by
    /// doing nothing, so it is also what tells a caller whether this one is worth resolving anything
    /// for.
    constexpr size_t kOffCharComponentRebuild  = 0x08;
    /// The sheet is owed a recomposition.
    constexpr uint8_t kCharRebuildSheet   = 0x01;
    /// The geometry is owed a fresh decision about which pieces are visible.
    ///
    /// The two are NOT independent, and the asymmetry is easy to get backwards: the per-frame entry
    /// tests the sheet bit first and only reaches the geometry branch when that bit is clear and no
    /// region is dirty. So asking for the sheet alone also postpones the geometry, and asking for
    /// geometry by setting the sheet bit asks for the one thing that cannot happen.
    constexpr uint8_t kCharRebuildGeosets = 0x04;
    /// How many of the ten regions the stock arrangement spends on the body. The two past it are the
    /// scalp, which a layout that is not square gives a block of its own beside the arrangement.
    constexpr uint32_t kCharStockRegionCount = 8;
    constexpr size_t kOffCharComponentSheet    = 0x190;
    constexpr size_t kOffCharComponentTextures = 0x194;
    /// The component REQUEST, which is what the sheet texture is ultimately fed from: its source
    /// callback follows this pointer to an image and hands the card a level of it. Not an image
    /// itself -- requests are allocated from free lists, queued to a composition thread of their own,
    /// and carry their own mipped image sized from the resolution figure.
    constexpr size_t kOffCharComponentComposeRequest = 0x52C;
    /// Override creature skin substitution, the mechanism behind displayid-driven recolours. __cdecl,
    /// caller-cleaned.
    constexpr uintptr_t kCharReplaceMonsterSkin            = 0x004F20C0;
    /// Bind extension state to a character component as it attaches to its model. __thiscall, 2 stack
    /// args.
    constexpr uintptr_t kCharInit                          = 0x004F24D0;
    /// The dispatcher every equipped item goes through, hookable to add slots or redirect item display
    /// data. __stdcall, 3 stack args.
    constexpr uintptr_t kCharAddItemBySlot                 = 0x004F2880;

    // header array de-relocation
    /// De-relocate the skin profile's texture-unit array, the one skin sub-array not yet covered, so
    /// modern skin layouts can be remapped. __cdecl, caller-cleaned.
    constexpr uintptr_t kReadSkinTextureUnits              = 0x00835B30;

    // model cache and its worker thread
    /// Run extension setup at the exact moment the model cache and its worker thread come up, before
    /// any model can be requested. __thiscall, 1 stack arg.
    constexpr uintptr_t kCacheInitialize                   = 0x0081C0D0;
    /// Intercept every shared-model creation and cache lookup by path, so an extension can substitute
    /// or redirect a model file before any parsing happens. __thiscall, 2 stack args.
    constexpr uintptr_t kCacheCreateShared                 = 0x0081C390;
    /// Observe the per-tick sweep of the shared-model update list, the natural place to drive streaming
    /// or deferred work on loaded models. __thiscall, caller-cleaned.
    constexpr uintptr_t kCacheUpdateShared                 = 0x0081C790;

    // other model entries
    /// Watch model lights being linked into the active light list, so extensions can cap or prioritise
    /// them. __thiscall, caller-cleaned.
    constexpr uintptr_t kLinkLight                         = 0x00834C70;
    /// Change how a single light accumulates into the model's diffuse term, the hook for a different
    /// falloff or attenuation curve. __thiscall, 2 stack args.
    constexpr uintptr_t kAddDiffuseLight                   = 0x00834DC0;
    /// Inject extension-owned dynamic lights into the model lighting solve, or reject stock ones.
    /// __stdcall, 1 stack arg.
    constexpr uintptr_t kAddLight                          = 0x00834F60;
    /// Intercept the transform of light positions into camera space, the last stage before constants
    /// are uploaded. __thiscall, caller-cleaned.
    constexpr uintptr_t kLightingToCameraSpace             = 0x008350A0;
    /// Replace the directional sun term used on models, letting an extension drive it from its own sky
    /// or time-of-day data. __thiscall, caller-cleaned.
    constexpr uintptr_t kSetupSunlight                     = 0x00835280;
    /// Rewrite the fixed-function and shader light state models are rendered with, the core of any
    /// modern lighting replacement. __thiscall, 1 stack arg.
    constexpr uintptr_t kSetupGxLights                     = 0x008353D0;
    /// Read back the resolved sun light, which other systems query to match their own shading to
    /// models. __thiscall, 4 stack args.
    constexpr uintptr_t kGetSunLight                       = 0x008355D0;
    /// Override the fog parameters applied to models specifically, independent of terrain fog.
    /// __thiscall, caller-cleaned.
    constexpr uintptr_t kSetupGxFog                        = 0x00835750;
    /// Catch the global particle system flush, where extension-owned emitters must also be released.
    /// __thiscall, caller-cleaned.
    constexpr uintptr_t kParticleSystemFlush               = 0x00981000;

    // particle emitters
    /// Integrate one particle's motion, the finest hook for adding gravity fields, wind or bounces.
    /// __thiscall, 2 stack args.
    constexpr uintptr_t kParticleMoveOne                   = 0x00979BB0;
    /// Replace evaluation of every emitter parameter track at once, needed for modern emitter track
    /// encodings. __thiscall, 5 stack args.
    constexpr uintptr_t kParticleInterpolateTracks         = 0x00979E90;
    /// Set up the shared render state for an emitter before its particles are emitted into the buffer.
    /// __thiscall, 2 stack args.
    constexpr uintptr_t kParticleRenderPrep                = 0x0097A390;
    /// The buffer-level vertex emit shared by the batched and unbatched particle paths, one hook
    /// covering both. __stdcall, 4 stack args.
    constexpr uintptr_t kParticleEmitVertices              = 0x0097A580;
    /// Per-particle emission of geometry, fine enough to add per-particle culling or custom quad
    /// shapes. __thiscall, 2 stack args.
    constexpr uintptr_t kParticleRenderSingle              = 0x0097A670;
    /// Drive per-emitter colour replacement, the mechanism behind tinted and recoloured spell effects.
    /// __thiscall, 3 stack args.
    constexpr uintptr_t kParticleSetColors                 = 0x0097A990;
    /// The emitter update that runs beneath the public one, hookable to change emission timing without
    /// touching draw code. __thiscall, 2 stack args.
    constexpr uintptr_t kParticleInternalUpdate            = 0x0097ACB0;
    /// Adjust how live particles follow a moving parent, the behaviour behind world-space versus local-
    /// space trails. __thiscall, 1 stack arg.
    constexpr uintptr_t kParticleChangeFrameOfReference    = 0x0097AEF0;
    /// The full per-particle vertex construction, covering every emitter style, and thus where modern
    /// emitter features are implemented. __thiscall, 2 stack args.
    constexpr uintptr_t kParticleBuildVertex               = 0x0097BE80;
    /// Control the fast-path classification of an emitter, which decides whether the cheap or full
    /// simulation runs. __thiscall, caller-cleaned.
    constexpr uintptr_t kParticleDetermineIfSimple         = 0x0097D370;
    /// Own one fixed simulation step, where extension forces or collision can be added to particle
    /// motion. __thiscall, 2 stack args.
    constexpr uintptr_t kParticleStepUpdate                = 0x0097DD20;
    /// Own the emitter's vertex buffer fill, the place to change particle vertex format or capacity.
    /// __thiscall, 3 stack args.
    constexpr uintptr_t kParticleBuildVertexBuffer         = 0x0097E580;
    /// Take over the emitter's draw submission, including its material and blend selection. __thiscall,
    /// 2 stack args.
    constexpr uintptr_t kParticleRenderBatch               = 0x0097E730;
    /// Position the sub-models spawned by a model-emitting particle system, which stock code offers no
    /// control over. __thiscall, 1 stack arg.
    constexpr uintptr_t kParticlePlaceModels               = 0x0097E8D0;
    /// Wrap a whole emitter's per-frame simulation, the single place to rate-limit or retime particle
    /// systems. __thiscall, 4 stack args.
    constexpr uintptr_t kParticleEmitterUpdate             = 0x0097EB10;

    // per-instance model state
    /// Supply a model's static bounding box, the value culling and selection both trust. __thiscall, 1
    /// stack arg.
    constexpr uintptr_t kGetBoundingBox                    = 0x004F5E20;
    /// Decide whether a model may render before its textures finish loading, which controls pop-in
    /// behaviour. __thiscall, caller-cleaned.
    constexpr uintptr_t kCanUpdateNonReadyTextures         = 0x00823E40;
    /// The blocking wait a model load funnels through, which must be diverted before any asynchronous
    /// model loading is safe. __thiscall, 1 stack arg.
    constexpr uintptr_t kWaitForLoad                       = 0x00823ED0;
    /// Intercept animation enable/disable per model, the cheapest lever for culling animation cost at
    /// distance. __thiscall, 1 stack arg.
    constexpr uintptr_t kSetAnimating                      = 0x00823F10;
    /// Reach a model's embedded cameras, which cinematics and portrait framing drive from. __thiscall,
    /// 1 stack arg.
    constexpr uintptr_t kGetCameraByIndex                  = 0x00824170;
    /// Per-model ribbon toggle, an obvious quality knob and a way to force trails on for specific
    /// models. __thiscall, 1 stack arg.
    constexpr uintptr_t kSetRibbonsEnabled                 = 0x00824230;
    /// Propagate a coordinate-space change to a model and everything attached to it, including its
    /// emitters. __thiscall, 1 stack arg.
    constexpr uintptr_t kChangeFrameOfReference            = 0x00824460;
    /// Report load progress including extension-managed assets, so loading screens do not finish early.
    /// __thiscall, 2 stack args.
    constexpr uintptr_t kComputeLoadProgress               = 0x008245B0;
    /// Catch release of a model's textures and attached objects, distinct from its own destruction.
    /// __thiscall, caller-cleaned.
    constexpr uintptr_t kFreeExternalResources             = 0x00824D30;
    /// The readiness predicate the entire client gates on, so an extension-managed load can report
    /// completion on its own terms. __thiscall, 2 stack args.
    constexpr uintptr_t kIsLoaded                          = 0x00824F00;
    /// The transition into the loaded state, the precise moment extension-side post-load work should
    /// run. __thiscall, caller-cleaned.
    constexpr uintptr_t kUpdateLoadedState                 = 0x00825170;
    /// The lighter placement entry used by UI and preview models, hookable independently of the full
    /// one. __thiscall, 3 stack args.
    constexpr uintptr_t kSetWorldTransformSimple           = 0x008251D0;
    /// Recolour a model's emitters from outside, the model-level counterpart to the emitter colour
    /// call. __thiscall, 4 stack args.
    constexpr uintptr_t kReplaceParticleColor              = 0x00825410;
    /// Override the animated bounding box, which is what makes an extension-deformed model cull
    /// correctly. __thiscall, 1 stack arg.
    constexpr uintptr_t kGetCurrentBoundingBox             = 0x00825750;
    /// Per-body-part bounds, used for partial visibility and for aiming effects at a region of a
    /// character. __thiscall, 2 stack args.
    constexpr uintptr_t kGetSplitBodyBoundingBox           = 0x00825A60;
    /// Catch the teardown of that compacted list so extension-side batch metadata is invalidated in
    /// step. __thiscall, caller-cleaned. Also the "the visibility array moved, rebuild from it"
    /// request every geoset change ends with.
    constexpr uintptr_t kUnoptimizeVisibleGeometry         = 0x00825D70;
    using M2_UnoptimizeVisibleGeometryFn = void(__fastcall*)(void* instance, void* edx);
    /// Declare sequences present that the stock file does not contain, so callers take the path an
    /// extension wants. __thiscall, 1 stack arg.
    constexpr uintptr_t kHasSequence                       = 0x00825EE0;
    /// Change which animation the client falls back to when a requested sequence is absent, so new
    /// sequence IDs degrade sensibly. __thiscall, 2 stack args.
    constexpr uintptr_t kResolveSequenceFallback           = 0x00826350;
    /// Toggle per-bone behaviour such as billboarding or external control from an extension.
    /// __thiscall, 3 stack args.
    constexpr uintptr_t kSetBoneFlags                      = 0x008265E0;
    /// Expose or fake the currently playing sequence state, which gameplay and UI code reads
    /// constantly. __thiscall, 2 stack args.
    constexpr uintptr_t kGetBoneSequenceInfo               = 0x008266B0;
    /// The cheapest query for what a model is playing right now, the natural anchor for an animation-
    /// state event. __thiscall, 1 stack arg.
    /// GetBoneSequenceId(slot) -> seqId, slot=-1 = primary. Confirmed alongside SetBoneSequence's
    /// own slot param, which mirrors this same concept (WXL-32/33, wxl-equip-extension) -- slot is
    /// a key_bone_id resolved through kOffHdrKeyBoneCount/kOffHdrKeyBoneArray (keyBoneLookup), same
    /// as SetBoneSequence's own slot param; returns kOffRtBonePendingSeq for the resolved bone.
    constexpr uintptr_t kGetBoneSequenceId                 = 0x008267E0;
    using M2_GetBoneSequenceIdFn = uint32_t(__fastcall*)(void* instance, void* edx, uint32_t slot);
    /// React when an animation is cut short rather than ending naturally, which stock code gives no
    /// notification for. __thiscall, 2 stack args.
    constexpr uintptr_t kOnSequenceInterrupted             = 0x008269C0;
    /// The shared preparation both sequence slots run through, so one hook covers blend times and
    /// looping for both. __thiscall, 5 stack args.
    constexpr uintptr_t kSetupBoneSequence                 = 0x00826B00;
    /// Intercept every primary animation start on a model, the main lever for animation remapping or
    /// blending policy. __thiscall, 6 stack args. This is what kSetBoneSequence's own `primary`
    /// param (confirmed 2026-08-19) tail-calls when true.
    constexpr uintptr_t kSetPrimaryBoneSequence            = 0x00826C40;
    /// Same for the secondary (upper-body) slot, which is what drives layered animation. __thiscall, 5
    /// stack args. This is what kSetBoneSequence's own `primary` param tail-calls when false --
    /// GetBoneSequenceId cannot see whatever this path sets (see kSetBoneSequence's own comment).
    constexpr uintptr_t kSetSecondaryBoneSequence          = 0x00826DD0;
    /// Scrub or freeze a running sequence, giving extensions frame-accurate control for cinematics and
    /// tools. __thiscall, 2 stack args.
    constexpr uintptr_t kSetBoneSequenceTime               = 0x00826ED0;
    /// Trigger point for streaming an external animation file, where a modern .anim can be fetched
    /// instead of the stock one. __thiscall, 1 stack arg.
    constexpr uintptr_t kLoadSequenceOnDemand              = 0x00827190;
    /// The sanctioned way to drive a bone from outside the animation system, hookable to validate or
    /// extend it. __thiscall, 2 stack args.
    constexpr uintptr_t kSetBoneProceduralTransform        = 0x008272F0;
    /// Advertise attachment points a stock model lacks, so callers proceed to place effects on
    /// extension-provided slots. __thiscall, 1 stack arg.
    constexpr uintptr_t kHasAttachment                     = 0x008273D0;
    /// Read an attachment's model-space pivot before animation, the basis for remapping attachment IDs
    /// across model versions. __thiscall, 2 stack args.
    constexpr uintptr_t kGetAttachmentPivot                = 0x00827460;
    /// Observe every attach/detach teardown, the point where extension state bound to a parent-child
    /// model link must be dropped. __thiscall, caller-cleaned.
    constexpr uintptr_t kDetachFromParent                  = 0x008274F0;
    /// Declare animation events a stock model does not carry, letting extensions add event-driven
    /// behaviour to existing models. __thiscall, 1 stack arg.
    constexpr uintptr_t kHasEvent                          = 0x008275F0;
    /// Per-model particle toggle, the same knob for emitters. __thiscall, 1 stack arg.
    constexpr uintptr_t kSetEmittersEnabled                = 0x008279F0;
    /// Report the batch count the renderer sizes its buffers from, which must reflect any extension-
    /// added batches. __thiscall, caller-cleaned.
    constexpr uintptr_t kCountGeometryBatches              = 0x00827A90;
    /// Replace the keyframe search shared by every animation track, which is what a new interpolation
    /// or track encoding must go through. __stdcall, 5 stack args.
    constexpr uintptr_t kFindTrackKey                      = 0x008284D0;
    /// Own the per-instance index selection, the hook for per-model LOD or geoset-driven index subsets.
    /// __thiscall, caller-cleaned.
    constexpr uintptr_t kSetModelIndices                   = 0x00828F90;
    /// Assemble the index buffer: walk the compacted draw list, and for each visible section append
    /// its own block of the skin's index array, so the buffer holds exactly the visible geometry
    /// back to back. Guarded by the buffer's own dirty flags, so it only rebuilds when something moved.
    ///
    /// It reads the block's start as a PLAIN 16-BIT FIELD (`movzx ecx, word ptr [esi+8]`, at both the
    /// memcpy and the CPU-rebase site). A skin whose sections carry the extended (level << 16)
    /// start therefore has every block past 65535 sourced from the wrong place, which draws as
    /// triangles joining unrelated vertices. client/CM2Shared/WideIndices.cpp detours it and refills
    /// the buffer from the widened start, for the sections whose widened start the skin can contain.
    using M2_SetModelIndicesFn = uint32_t(__fastcall*)(void* instance, void* edx);
    /// The two byte-verified read sites, one complete `movzx r32, word ptr [esi+8]` each (4 bytes) --
    /// too short to redirect in place, which is why the fold is done by detouring the whole fill.
    constexpr uintptr_t kSetModelIndicesSrcCopy   = 0x00829091;
    constexpr uintptr_t kSetModelIndicesSrcRebase = 0x008290BC;

    /// The shared-model sibling of kSetModelIndices: fills ONE index buffer per shared model, all
    /// submeshes in skin order, each repeated kOffSharedInstanceCopies times for the batched-doodad
    /// draw, then writes each submesh copy's own indexStart back as its offset in that buffer.
    /// Unlike kSetModelIndices it also CREATES the pool/buffer pair on first use.
    ///
    /// Same plain 16-bit source read (`movzx ebx, word ptr [ebx+ecx+8]` at kSharedSetIndicesSrc), so
    /// it carries the identical ceiling. thiscall (ECX = shared model), no stack args -> bool.
    ///
    /// This is also the place a 32-bit or re-split index scheme has to be installed: it owns the
    /// index-buffer upload for a shared model outright.
    constexpr uintptr_t kSharedSetIndices    = 0x008360A0;
    constexpr uintptr_t kSharedSetIndicesSrc = 0x0083619F;
    using M2_SharedSetIndicesFn = uint32_t(__fastcall*)(void* model, void* edx);
    constexpr size_t kOffSharedIndexPool     = 0x178; ///< -> GxPool backing the shared index buffer
    constexpr size_t kOffSharedIndexBuf      = 0x17C; ///< -> the shared index GxBuf (built/valid at +0x1C/+0x1D)
    /// uint32: how many copies of the model one shared index buffer holds, for the batched-doodad
    /// draw. Read as the pool-size multiplier (`imul eax, [esi+0x190]`) and as the inner repeat count
    /// of the fill. Distinct from kOffSharedMaxInstances (0x194), which is the CEILING finalize
    /// computed; this is the count in force.
    constexpr size_t kOffSharedInstanceCopies = 0x190;
    /// -> the shared file record, whose flags dword at kOffM2FileFlags picks how both fills spell an
    /// index: bit 0x8, or bit 0x40 on a model with exactly one bone, means the skin's indices are
    /// copied as they stand; otherwise each submesh is rebased onto its own vertex window.
    constexpr size_t   kOffSharedFileRecord   = 0x04;
    constexpr size_t   kOffM2FileFlags        = 0x04;
    constexpr uint32_t kM2FileFlagGlobalIndices = 0x8;
    constexpr uint32_t kM2FileFlagSingleBoneGlobal = 0x40;
    /// The three sites where kDrawTriangleBatch turns the submesh it is about to draw into the draw
    /// call's StartIndex -- `movzx r32, word ptr [esi+8]` again, feeding a 32-bit field. This is the
    /// last 16-bit link in the index path: the fill sites above decide WHICH triangles land in the
    /// buffer, these decide where the draw starts reading it.
    constexpr uintptr_t kDrawBatchStartIndexSites[] = { 0x008205DD, 0x00820666, 0x008206DE };
    /// Per-instance geometry context: the compacted draw list plus the buffers it feeds.
    constexpr size_t kOffInstGeometryCtx  = 0x2D0;
    constexpr size_t kOffGeoCtxGroups     = 0x08; ///< -> group records, stride kGeoCtxGroupStride
    constexpr size_t kOffGeoCtxGroupCount = 0x0C; ///< uint32
    constexpr size_t kOffGeoCtxRanges     = 0x10; ///< -> {firstBatch, lastBatch} pairs, stride 8
    constexpr size_t kOffGeoCtxIndexBuf   = 0x18; ///< -> the index GxBuf this fills
    constexpr size_t kGeoCtxGroupStride   = 0x30; ///< a group's first dword indexes kOffGeoCtxRanges
    /// The index buffer's two "already built" flags; both set means the contents still stand.
    constexpr size_t kOffGxBufBuilt       = 0x1C;
    constexpr size_t kOffGxBufValid       = 0x1D;
    /// Device vtable slots the assembly rides: lock returns the writable buffer, unlock commits it.
    constexpr size_t kGxVtblBufLock       = 0xD8;
    constexpr size_t kGxVtblBufUnlock     = 0xDC;
    using Gx_BufLockFn   = void*(__thiscall*)(void* device, void* buffer);
    using Gx_BufUnlockFn = void(__thiscall*)(void* device, void* buffer, uint32_t flag);
    /// Bind the assembled buffer as the current index source. The receiver is the DEVICE, not the
    /// buffer: it compares against and updates the device's own current-index slot, and the buffer
    /// arrives on the stack (ret 4).
    constexpr uintptr_t kPrimIndexPtr                      = 0x00682F10;
    using Gx_PrimIndexPtrFn = void(__thiscall*)(void* device, void* buffer);
    /// Bind the vertex stream for one model instance, including any extension-supplied skinned buffer.
    /// __thiscall, 3 stack args.
    constexpr uintptr_t kSetModelVertices                  = 0x00829160;
    /// Compute per-region screen or world bounds, used to size projected effects against a model.
    /// __thiscall, 3 stack args.
    constexpr uintptr_t kGetRegionBounds                   = 0x008292A0;
    /// Filter which model batches contribute to the shadow map, separately from the main pass. __cdecl,
    /// caller-cleaned.
    constexpr uintptr_t kRenderBatchListShadowMap          = 0x00829E40;
    /// Own CPU vertex skinning for a model, the fallback path taken whenever hardware skinning is off.
    /// __cdecl, caller-cleaned.
    constexpr uintptr_t kTransformVertices                 = 0x00829F40;
    /// Attach extension-owned per-instance state at the moment a model object comes into existence.
    /// __thiscall, caller-cleaned.
    constexpr uintptr_t kModelConstruct                    = 0x0082BE60;
    /// Catch release of the model's internal animation and geometry buffers. __thiscall, caller-
    /// cleaned.
    constexpr uintptr_t kFreeInternalResources             = 0x0082C1C0;
    /// Same for the bounding sphere used by the cheaper distance and visibility tests. __thiscall, 1
    /// stack arg.
    constexpr uintptr_t kGetCurrentBoundingSphere          = 0x0082C2C0;
    /// Intercept every geoset show/hide, the single choke point for character geoset logic and for
    /// remapping modern geoset numbering. __thiscall, 3 stack args.
    constexpr uintptr_t kSetGeometryVisible                = 0x0082C7C0;
    /// Show or hide every geoset whose id falls in [idStart, idEnd]. It resolves ids to submeshes by
    /// scanning the skin's own submesh table, so it carries no notion of geoset GROUPS and no ceiling
    /// on their numbering: an id a stock model would never carry is shown or hidden like any other.
    /// The receiver is the per-instance object that owns the visibility array, not the shared model.
    ///
    /// The scan reads a FULL DWORD at the submesh's offset 0 and compares that against the range, so
    /// the id it matches on is really `(level << 16) | skinSectionId`. On a stock skin level is 0 and
    /// this is invisible. On a skin that carries a 32-bit index start in level, every section past the
    /// 16-bit line presents itself as `65536 + id`, falls outside every range anyone asks for, and can
    /// therefore never be hidden: it draws for the rest of the model's life, alongside every other
    /// variant of its own group.
    ///
    /// Before the instance is initialized (init flag bit 0 clear at kOffInstInitFlags) the call is
    /// queued as a deferred command instead of acting, and replayed later through
    /// kInitializeLoaded's opcode 1.
    using M2_SetGeometryVisibleFn =
        void(__fastcall*)(void* instance, void* edx, uint32_t idStart, uint32_t idEnd, uint32_t visible);
    /// The scan ends by asking for the compacted draw list to be rebuilt, but only when at least one
    /// entry actually flipped: see kUnoptimizeVisibleGeometry above.
    /// Control the merge of visible geosets into a compacted draw list, where batch limits and merge
    /// rules can be relaxed. __thiscall, caller-cleaned.
    constexpr uintptr_t kOptimizeVisibleGeometry           = 0x0082C970;
    /// Rebuild the compacted draw list from the visibility array. Nothing else reads that array, so a
    /// visibility change not followed by this call is a change with no effect. Same receiver as
    /// kSetGeometryVisible.
    using M2_OptimizeVisibleGeometryFn = void(__fastcall*)(void* instance, void* edx);
    /// Report or rewrite a sequence's timing and flags, the query nearly all animation logic asks
    /// before playing anything. __thiscall, 3 stack args.
    constexpr uintptr_t kGetSequenceInfo                   = 0x0082CED0;
    /// The total-batch query including particles and ribbons, used for draw budgeting and statistics.
    /// __thiscall, caller-cleaned.
    constexpr uintptr_t kCountBatches                      = 0x0082D1A0;
    /// Take over UV animation evaluation, needed for modern texture-transform track types and for
    /// scripted UV effects. __thiscall, caller-cleaned.
    constexpr uintptr_t kAnimateTextureTransforms          = 0x0082D6F0;
    /// The multi-sample vertex variant, which must be hooked alongside the plain one or half the draws
    /// bypass the override. __thiscall, 2 stack args.
    constexpr uintptr_t kSetModelVerticesMultiSample       = 0x0082D910;
    /// Intercept the full placement of a model in the world, including scale and orientation, before
    /// its matrix is built. __thiscall, 5 stack args.
    constexpr uintptr_t kSetWorldTransformFull             = 0x0082DD80;
    /// Adjust attached child models after their parent bones resolve, the hook for extension-driven
    /// attachment offsets. __thiscall, caller-cleaned.
    constexpr uintptr_t kAnimateAttachments                = 0x0082E550;
    /// The per-entry worker behind sequence callbacks, hookable when only individual notifications
    /// matter. __thiscall, 4 stack args.
    constexpr uintptr_t kProcessSequenceCallback           = 0x0082E790;
    /// Supply collision triangles for a model, the single funnel through which extension-provided
    /// collision geometry must enter. __thiscall, 3 stack args.
    constexpr uintptr_t kGetCollisionFacets                = 0x0082EC30;
    /// Own the single-threaded particle advance for a model, which is where emitter behaviour can be
    /// extended without touching the MT path. __thiscall, 2 stack args.
    constexpr uintptr_t kAnimateParticlesST                = 0x008309C0;
    /// Wrap one model's full animation evaluation, the natural place to add procedural bone work or
    /// skip animation entirely. __thiscall, caller-cleaned.
    constexpr uintptr_t kModelAnimate                      = 0x00830DC0;
    /// Reach every M2 animation event as it fires, the direct route from model timelines to extension
    /// and Lua handlers. __thiscall, 7 stack args.
    constexpr uintptr_t kDispatchEventCallbacks            = 0x00830FB0;
    /// Resolve or override where an attachment point sits in world space, which is what spell and
    /// weapon effects anchor to. __thiscall, 2 stack args.
    constexpr uintptr_t kGetAttachmentPosition             = 0x00831330;
    /// Get the full oriented attachment matrix, needed to place extension-owned models on bones
    /// correctly. __thiscall, 2 stack args.
    constexpr uintptr_t kGetAttachmentWorldTransform       = 0x00831410;
    /// Resolve where an animation event fires in world space, which sound and effect spawning depends
    /// on. __thiscall, 2 stack args.
    constexpr uintptr_t kGetEventPosition                  = 0x008317E0;
    /// Control the reduced animation path used for shadow-only models, useful for cutting shadow cost.
    /// __thiscall, caller-cleaned.
    constexpr uintptr_t kAnimateShadowModel                = 0x00831990;
    /// Per-model lighting setup, the right granularity for making one model light differently from the
    /// rest of the scene. __thiscall, caller-cleaned.
    constexpr uintptr_t kModelSetupLighting                = 0x00831AF0;
    /// Catch sequence starts queued while the animation file is still loading, the path that decides
    /// what plays once it arrives. __thiscall, 9 stack args.
    constexpr uintptr_t kSetBoneSequenceDeferred           = 0x00831C30;
    /// See sequence-completion notifications before the registered callback runs, for chaining or
    /// suppressing them. __thiscall, 1 stack arg.
    constexpr uintptr_t kDispatchSequenceCallback          = 0x00831FC0;
    /// Drive the per-model callback drain each frame, one place to add extension-owned deferred work
    /// per model. __thiscall, caller-cleaned.
    constexpr uintptr_t kProcessCallbackList               = 0x00832260;
    /// Release that per-instance state exactly once, at the client's own teardown point. __thiscall,
    /// caller-cleaned.
    constexpr uintptr_t kModelDestruct                     = 0x00832640;
    /// Observe every animation stop, needed to keep extension-side animation state from leaking across
    /// sequence changes. __thiscall, 3 stack args.
    constexpr uintptr_t kUnsetBoneSequence                 = 0x00832840;
    /// Observe models entering the render scene, giving extensions a reliable per-instance registration
    /// event. __thiscall, 1 stack arg.
    constexpr uintptr_t kAttachModelToScene                = 0x00834540;
    /// See a model instance bound to its shared data, the first point where both the file and the
    /// instance are known. __thiscall, 4 stack args.
    constexpr uintptr_t kModelInitialize                   = 0x00834810;

    // ribbon emitters
    /// Drive a ribbon's emission point directly, letting an extension steer a trail independently of
    /// its bone. __thiscall, 3 stack args.
    constexpr uintptr_t kRibbonSetPos                      = 0x0097F940;
    /// Swap a ribbon's texture at runtime, the ribbon counterpart of the already-known model texture
    /// replacement. __thiscall, 2 stack args.
    constexpr uintptr_t kRibbonReplaceTexture              = 0x0097FAD0;
    /// Control how an existing ribbon reacts to its parent teleporting or being re-parented.
    /// __thiscall, 1 stack arg.
    constexpr uintptr_t kRibbonChangeFrameOfReference      = 0x0097FBE0;
    /// Own ribbon segment simulation, which determines trail length, droop and lifetime. __thiscall, 2
    /// stack args.
    constexpr uintptr_t kRibbonUpdate                      = 0x00980090;
    /// Intercept ribbon construction parameters at load time, the place to apply modern ribbon fields.
    /// __thiscall, 9 stack args.
    constexpr uintptr_t kRibbonInitialize                  = 0x009808A0;

    // scene graph, hit testing and collision
    /// Grow the hit-result list, the cap that silently truncates picking in dense scenes. __thiscall,
    /// caller-cleaned.
    constexpr uintptr_t kAllocateHitList                   = 0x0081CAD0;
    /// Change the broad-phase sphere cull that decides which models are even considered for a hit test.
    /// __thiscall, 4 stack args.
    constexpr uintptr_t kSphereTestModels                  = 0x0081CFF0;
    /// Replace ray-versus-model-geometry picking, so extension-owned or modern-format meshes become
    /// selectable. __thiscall, 8 stack args.
    constexpr uintptr_t kHitTestGeometry                   = 0x0081DAF0;
    /// Replace ray-versus-model-collision-mesh testing, which is what physics and line-of-sight queries
    /// actually consult. __thiscall, 8 stack args.
    constexpr uintptr_t kHitTestCollisionMesh              = 0x0081DD50;
    /// Post-process the finished model hit list, to filter results or inject hits from extension-
    /// managed models. __thiscall, 4 stack args.
    constexpr uintptr_t kEndHitTest                        = 0x0081DF10;
    /// Same for the world-collision variant used by movement queries, where a missing model hit becomes
    /// a walk-through bug. __thiscall, 3 stack args.
    constexpr uintptr_t kEndHitTestCollisionWorld          = 0x0081E110;
    /// Replace the per-model light selection, the gate that decides which scene lights a model is
    /// allowed to see. __thiscall, 1 stack arg.
    constexpr uintptr_t kSceneSelectLights                 = 0x0081E400;
    /// Control ribbon draw ordering separately from geometry, which decides how overlapping trails
    /// composite. __cdecl, caller-cleaned.
    constexpr uintptr_t kSortOpaqueRibbonElements          = 0x0081ED10;
    /// Control the opaque particle ordering pass independently of the additive one. __cdecl, caller-
    /// cleaned.
    constexpr uintptr_t kSortOpaqueParticleElements        = 0x0081EDF0;
    /// Reorder the opaque model batch list, for instance to group by material and cut state changes.
    /// __cdecl, caller-cleaned.
    constexpr uintptr_t kSortOpaqueElements                = 0x0081EEA0;
    /// Change the transparent-element ordering, the usual fix when modern models draw their alpha
    /// batches in the wrong order. __cdecl, caller-cleaned.
    constexpr uintptr_t kSortTransparentElements           = 0x0081EF30;
    /// Rewrite the vertex/pixel shader pair chosen for a single draw element just before it is sorted
    /// and drawn. __stdcall, 1 stack arg.
    constexpr uintptr_t kSceneComputeElementShaders        = 0x0081F1D0;
    /// Intercept the sampler binding for every model batch, the hook for injecting extra maps or
    /// overriding a texture per batch. __thiscall, caller-cleaned.
    constexpr uintptr_t kSetupBatchTextures                = 0x0081F450;
    /// Choose which vertex stream a batch draws from, including the fixed-function versus programmable
    /// split. __stdcall, 1 stack arg.
    constexpr uintptr_t kUploadBatchVertices               = 0x0081F700;
    /// Observe and adjust model cloning, which is how mirrored and preview models are spawned from an
    /// existing instance. __thiscall, 2 stack args.
    constexpr uintptr_t kSceneDuplicateModel               = 0x0081F970;
    /// Rewrite the light and ambient constants uploaded for each model batch, the entry point for a
    /// modern lighting model on characters and doodads. __thiscall, caller-cleaned.
    constexpr uintptr_t kSetupBatchLighting                = 0x0081FB10;
    /// Take over the projected-texture batch path, which is how decals and projected effects land on
    /// models. __thiscall, caller-cleaned.
    constexpr uintptr_t kDrawBatchProjected                = 0x00820720;
    /// Own the ribbon draw call and its state, needed to give trails a different blend or shader.
    /// __thiscall, caller-cleaned.
    constexpr uintptr_t kDrawRibbonBatch                   = 0x00820F40;
    /// Reach the merged multi-emitter particle draw, the hot path where particle batching can be
    /// widened or instanced. __thiscall, 4 stack args.
    constexpr uintptr_t kDrawBatchedParticles              = 0x00821100;
    /// Own the per-emitter particle draw, letting an extension change particle blending or route it to
    /// another pass. __thiscall, 4 stack args.
    constexpr uintptr_t kDrawParticleBatch                 = 0x008214E0;
    /// Wrap the whole per-frame model animation pass, giving an extension one place to add, skip or
    /// time-scale model updates. __thiscall, 1 stack arg.
    constexpr uintptr_t kSceneAnimate                      = 0x00821A20;
    /// Reach the element loop that actually issues every model, particle and ribbon draw, for per-
    /// element filtering or reordering. __thiscall, 4 stack args.
    constexpr uintptr_t kSceneRenderDraw                   = 0x00823130;
    /// Bracket the entire M2 draw pass, so an extension can push and restore its own render state
    /// around all model rendering. __thiscall, 1 stack arg.
    constexpr uintptr_t kSceneDraw                         = 0x00823CB0;

    // shared-model load, build and teardown
    /// Pair with the release hook to keep an extension's own per-shared-model table exactly in step
    /// with the client's refcounting. __thiscall, caller-cleaned.
    constexpr uintptr_t kSharedAddRef                      = 0x00835970;
    /// Shared-model refcount step, shape shared by kSharedAddRef and kSharedRelease: native this-in-ECX,
    /// no stack arguments. A reference is what keeps a shared model resident -- dropping to zero does not
    /// free it outright but parks it on the cache's reclaim list, where the next reference revives it in
    /// place and a later sweep is free to take it instead. Holding one past the model that acquired it is
    /// therefore the way to guarantee a variant stays loaded, and the only way to leak one.
    using M2_SharedRefFn = void(__fastcall*)(void* shared, void* edx);
    /// Insert an extension continuation into the load-completion chain of a shared model without
    /// polling. __thiscall, 1 stack arg.
    constexpr uintptr_t kSharedCallbackWhenLoaded          = 0x008359C0;
    /// Own the vertex-buffer upload for a shared model, including its vertex format choice, before any
    /// instance draws. __thiscall, 1 stack arg.
    ///
    /// With kEnableShaders set it fills at kModelVertexStride: per co-instance, per section, per skin
    /// vertex v of that section it copies the whole model vertex record named by
    /// skin->vertexLookup[v] into slot v, then rewrites kOffVertexBoneSlots alone from
    /// skin->bones[v * 4], adding section->boneCount * coInstance to each of the four bytes as one
    /// dword add. Position and weights therefore reach the GPU exactly as the record carries them.
    /// With shaders off it packs a narrower vertex instead and deposits no bone slots at all, and
    /// fills a single block rather than one per co-instance.
    ///
    /// Which of the two runs is decided by the global shader-enable flag, NOT by the stack argument:
    /// that argument selects a texture-coordinate pair for the narrow path only, and the wide path
    /// overwrites it outright and uses it as its own loop cursor. Either path walks a submesh's own
    /// range, [vertexStart, vertexStart + vertexCount), rather than the skin's vertices flat.
    constexpr uintptr_t kSharedSetVertices                 = 0x008362B0;
    using M2_SharedSetVerticesFn = uint32_t(__fastcall*)(void* shared, void* edx, int texCoordSet);
    /// The vertex GxBuf the fill writes into, and the pool backing it. Both null until the first call
    /// creates them; the buffer then carries kOffGxBufBuilt/kOffGxBufValid like the index one, and the
    /// fill is skipped outright while both stand, leaving only the rebind.
    constexpr size_t kOffSharedVertexPool = 0x180;
    constexpr size_t kOffSharedVertexBuf  = 0x184;
    /// The model vertex record, as kReadVertices lays it down and as the programmable fill copies it.
    constexpr size_t kModelVertexStride   = 0x30;
    constexpr size_t kOffVertexPosition   = 0x00; // float[3]
    constexpr size_t kOffVertexWeights    = 0x0C; // uint8[4], one per slot, summing to 255
    constexpr size_t kOffVertexBoneSlots  = 0x10; // uint8[4] palette slots; the one field the fill rewrites
    /// Catch release of a model's GPU vertex and index pools, needed to keep extension-owned buffers
    /// from outliving them. __thiscall, caller-cleaned.
    constexpr uintptr_t kSharedDestroyBuffers              = 0x008368B0;
    /// Resize the per-instance array a shared model hands to its model instances, the hook for raising
    /// instance limits. __thiscall, 1 stack arg.
    constexpr uintptr_t kSharedAllocInstances              = 0x00836DF0;
    /// Rewrite how per-batch texture values collapse into texture combos, the step that must change
    /// when a model carries more textures per batch than stock. __thiscall, caller-cleaned.
    constexpr uintptr_t kSharedConvertTextureCombos        = 0x00837250;
    /// Control which texture-combo index each render batch ends up pointing at, letting an extension
    /// re-bind batch materials wholesale. __thiscall, caller-cleaned.
    constexpr uintptr_t kSharedAssignBatchTextureCombos    = 0x008374A0;
    /// Override the per-batch shader specialization the client picks at load time, the entry point for
    /// modern M2 shader IDs. __thiscall, caller-cleaned.
    constexpr uintptr_t kSharedSubstituteSpecializedShaders = 0x00837680;
    /// Parse a streamed low-priority .anim payload yourself, which is required when the animation file
    /// uses a modern layout. __thiscall, 2 stack args.
    constexpr uintptr_t kSharedFinishLowPrioritySequence   = 0x0083CA90;
    /// Take over the read request for a model's main file, which is where a modern-format loader must
    /// divert to its own parser. __thiscall, 3 stack args.
    constexpr uintptr_t kSharedLoad                        = 0x0083D410;
    /// Free extension-side allocations hung off a shared model at the single point where the client
    /// tears the object down. __thiscall, caller-cleaned.
    constexpr uintptr_t kSharedDestroy                     = 0x0083D5B0;
    /// Track shared-model refcount drops so extension-owned side data attached to a model can be freed
    /// at the right moment. __thiscall, caller-cleaned.
    constexpr uintptr_t kSharedRelease                     = 0x0083DC90;
}
