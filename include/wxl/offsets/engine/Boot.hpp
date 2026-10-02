// The engine's one-shot initialisation routine.
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

// INTERNAL to the core. Modules never include this; they use wxl::game / wxl::events.
namespace wxl::offsets::engine::boot
{
    // Engine initialisation, run once from the client's own startup: it brings up the background
    // file reader and then the texture subsystem, in that order, along with the rest of the engine.
    // Reached indirectly, so it has no call site of its own.
    //
    // Its entry is the earliest point that is both on the main thread and outside the loader lock,
    // which is what makes it the place to load anything that must precede the reader queues or the
    // texture scratch sizing.
    constexpr uintptr_t kEngineInit = 0x004047E0;
    using EngineInitFn = uint32_t(__cdecl*)();
}
