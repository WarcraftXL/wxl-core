// Ground-effect (grass / detail doodad) renderer addresses.
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

// INTERNAL to the core. The ground-effect renderer entries and globals the grass features hook.
namespace wxl::offsets::game::groundeffect
{
    // --- entries ---
    // Per-chunk grass setup: picks the grass vertex shader, builds the chunk-local -> view matrix
    // into the constant block, and uploads the block as vertex constants c0..c22. Called once per
    // visible grass chunk per frame, on the shader path only. (mtx, group); group is 0 on the
    // live path (1 selects the dormant point-light shader permutations).
    constexpr uintptr_t kChunkConstantUpload = 0x007B10E0;
    using ChunkConstantUploadFn = void(__cdecl*)(const float* mtx, int group);

    // Per-frame grass shader-constant setup on the shader path: memsets the c0..c22 block at
    // kVsConstantBlock and fills it, once per frame at the top of the detail-doodad pass (only when the
    // shader path is active). Static void(void). Tail-hook it to publish per-frame values (e.g. wind) into
    // the free registers (kVsFirstFreeReg onward) every frame, so a swapped vertex shader never samples
    // stale constants.
    constexpr uintptr_t kInitShaderConstants = 0x007B15D0;
    using InitShaderConstantsFn = void(__cdecl*)();

    // First free vertex-constant register above the grass block (c0..c22) and the shadow block (c23..c34);
    // the wind feature uploads c35..c37 here. Free on the grass pass: nothing reads >= c35.
    constexpr unsigned kWindFirstReg = 35;
    constexpr unsigned kWindRegCount = 3; // c35 = {phase,windTime,amplitude,0}, c36 = {dir.x,dir.y,bias,scale}, c37 = spatial

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
    /// Grass/detail draw distance with the engine's own clamp and squared-distance bookkeeping done for
    /// you - safer than writing the two globals directly. __cdecl, caller-cleaned.
    constexpr uintptr_t kDetailDoodadDistSet               = 0x00780730;

    // --- globals ---
    // Vertex-shader constant block, float4[23] = c0..c22, memset to zero once per grass pass then
    // uploaded per chunk by kChunkConstantUpload. c0..c12 are live (chunk->view matrix, projection,
    // fog, distance fade, sun); c13 is shadowed by a shader-local def; c14..c22 are only read by
    // the dormant point-light permutations, so they are free on the live path.
    constexpr uintptr_t kVsConstantBlock = 0x00D1C518;
    constexpr unsigned  kVsConstantCount = 23;
    // First free register in the block (c14) and how many follow (c14..c22).
    constexpr unsigned  kVsFirstFreeReg  = 14;
    constexpr unsigned  kVsFreeRegCount  = 9;

    // Grass vertex-shader table: 2 groups of 3 shadow variants (group 1 = dormant point-light path).
    constexpr uintptr_t kVertexShaderTable = 0x00D1C4A8;

    // Grass draw-distance CVar validation cap: the float 140.0f compared in the CVar callback.
    constexpr uintptr_t kDistCapFloat = 0x009E8CFC;

    // Blade-count clamp immediates (0x1000) in the pool rebuild: count = density * 64, clamped.
    constexpr uintptr_t kDensityClampImms[4] = { 0x007B2AA6, 0x007B2ABA, 0x007B2AC5, 0x007B2ACC };

    // Pool-dirty flag: setting 1 makes the next pool update free every chunk's grass and rebuild.
    constexpr uintptr_t kPoolDirtyFlag = 0x00D1C4C0;
}
