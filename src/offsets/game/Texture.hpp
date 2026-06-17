// Texture upload patch site.
// Copyright (C) 2026 WraithEngine
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

// Client texture-upload patch sites.
namespace wraith::offsets::game::texture
{
    // The DXT blit dispatcher has a zero-copy fast path that points the GPU upload source straight at the
    // transient BLP file buffer instead of copying each mip into the managed scratch. That buffer is freed
    // (async load) or recycled (sync scratch) before the upload reads it, so a large DXT texture (one that
    // takes the non-pooled path, e.g. 1024x1024) uploads from a stale pointer and faults. The fast-path
    // gate is a `jne` taken when the fast-path flag is set; NOPing it forces every DXT load through the
    // mip-copy path. 6 bytes. Native-safe: the copy path is the engine's own correct branch and the scratch
    // is sized for the largest format at init, so smaller native textures are unaffected (one extra in-RAM
    // block copy per DXT load). Resolution is preserved - nothing is downscaled.
    constexpr uintptr_t kDxtZeroCopyGate    = 0x006B0075;
    constexpr size_t    kDxtZeroCopyGateLen = 6;
}
