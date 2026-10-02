// Terrain per-chunk draw entry addresses, their signatures, and draw-node field offsets.
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

// INTERNAL to the core. Terrain per-chunk draw entries and the variants the live render-state
// selector dispatches to.
namespace wxl::offsets::engine::draw
{
    // Candidate per-chunk terrain draw entries, each taking the render node. The first two take the
    // node in the this-register; the last two take it on the stack. Used to discover which variant the
    // live render-state selector dispatches to.
    constexpr uintptr_t kTerrainDrawV1 = 0x007D28B0; // node in this-register
    constexpr uintptr_t kTerrainDrawV2 = 0x007D2D70; // node in this-register
    constexpr uintptr_t kTerrainDrawV3 = 0x007D1AD0; // node on stack
    constexpr uintptr_t kTerrainDrawV4 = 0x007D2520; // node on stack

    // Render-node field: layer count (the draw-loop bound).
    constexpr size_t kOffNodeLayerCount = 0x09;

    // Node-in-this-register draw variant: native this-in-ECX; declared with a dummy second parameter
    // so the trampoline routes the node into the this-register.
    using Terrain_DrawNodeRegFn = void(__fastcall*)(void* node, void* edx);
    // Node-on-stack draw variant.
    using Terrain_DrawNodeStackFn = void(__cdecl*)(void* node);
}
