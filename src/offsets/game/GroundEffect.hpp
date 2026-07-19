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

    // Per-frame grass shader-constant setup on the shader path (CDetailDoodad::InitializeShaderConstants):
    // memsets the c0..c22 block at kVsConstantBlock and fills it, once per frame at the top of the
    // detail-doodad pass (only when the shader path is active). Static void(void). Tail-hook it to publish
    // the wind constants (c35..c37 — above everything the grass/shadow passes touch) into the free
    // registers every frame, so a swapped wind vertex shader never samples stale constants.
    constexpr uintptr_t kInitShaderConstants = 0x007B15D0;
    using InitShaderConstantsFn = void(__cdecl*)();

    // First free vertex-constant register above the grass block (c0..c22) and the shadow block (c23..c34);
    // the wind feature uploads c35..c37 here. Verified free on the grass pass (nothing reads >= c35).
    constexpr unsigned kWindFirstReg = 35;
    constexpr unsigned kWindRegCount = 3; // c35 = {phase,windTime,amplitude,0}, c36 = {dir.x,dir.y,bias,scale}, c37 = spatial

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
