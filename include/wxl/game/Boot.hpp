// Engine initialisation as a detourable point.
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

#include "wxl/offsets/engine/Boot.hpp"

/**
 * @brief The engine's initialisation routine, exposed as a detour target.
 *
 * It runs once, on the main thread, and brings up the background file reader before the texture
 * subsystem. Detouring its entry is how anything gets in ahead of either -- including the core's own
 * extension loading, which needs a point that is outside the loader lock and still early enough to
 * precede both.
 */
namespace wxl::game::boot
{
    namespace off = wxl::offsets::engine::boot;

    /// Entry of the engine initialisation routine.
    constexpr uintptr_t kEngineInit = off::kEngineInit;

    /// Its signature, for a detour and the matching trampoline.
    using EngineInitFn = off::EngineInitFn;
}
