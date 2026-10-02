// Per-frame pump entry and the frame-timing globals it refreshes.
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

// INTERNAL to the core. The master per-frame pump and the timing globals it updates each frame.
namespace wxl::offsets::engine::frame
{
    // Frame-time tick: the one writer of the frame delta and the frame timestamp, which it receives
    // as arguments. Everything in the client that asks "how long was the last frame" reads what this
    // stores, so it runs exactly once per frame by construction -- a second call would corrupt the
    // client's own timing before it corrupted ours. That makes it the OnUpdate anchor.
    //
    // __cdecl, two stack args, verified at the prologue ([ebp+8] float, [ebp+0xc] int) and at the
    // bare `ret` that ends it.
    constexpr uintptr_t kFramePump = 0x0077ECB0;
    using FramePumpFn = void(__cdecl*)(float deltaSeconds, uint32_t frameTimeMs);

    // The two globals it stores into, kept because a great deal of the client reads them and they
    // are the only place a late caller can recover the current frame's timing from.
    constexpr uintptr_t kDeltaSeconds = 0x00CD76A0; // float
    constexpr uintptr_t kFrameTimeMs  = 0x00CD76AC; // u32
}
