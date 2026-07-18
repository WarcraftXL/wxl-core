// FFI cdef generator: derives LuaJIT struct declarations from the canonical offsets/ constants.
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

#include <string>

/// LuaJIT FFI cdef text, generated from the same offsets/game constants the C++ struct views use,
/// so a game struct has ONE source of truth shared by C++ and Lua. Every declaration is formatted
/// FROM the offset symbols (never hand-copied hex), so a cdef can never drift from the C++ layout.
/// The declarations are packed to mirror the `#pragma pack(1)` C++ views exactly.
namespace wxl::lua::ffi
{
    /**
     * @brief The full cdef block for every struct the engine mirrors into FFI.
     *
     * Currently wxl_Unit (from unit::kUnitPositionField) and wxl_Model (from unit::kModelParentField).
     * GROWTH PATH: add one builder per struct, each deriving its padding from that struct's offsets/
     * constants, and append it here — one line per struct, never hand-written hex.
     * @return a NUL-free C string suitable for a single ffi.cdef() call.
     */
    std::string BuildCoreCdefs();
}
