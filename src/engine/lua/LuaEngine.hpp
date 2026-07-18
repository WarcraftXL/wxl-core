// Engine Lua VM host: lifecycle of the privileged LuaJIT state and its per-frame pump.
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

#include <cstddef>

struct lua_State;

/// Hosts the single privileged engine VM (LuaJIT / Lua 5.1) and is the one central .cpp: it owns
/// the state's lifecycle, assembles the global `wxl` table from the method contexts, wires the
/// event contexts through the bridge, and drives the loader. The state is created, used and
/// destroyed on ONE thread — the game/render thread, since the 12340 client is single-threaded for
/// render and logic. The thread id is latched at Start() and asserted (debug only) at every entry
/// point. The engine registers itself as a feature and boots on the first frame, so no bootstrap
/// change beyond InstallRegisteredFeatures() is required; Start()/Stop() are also public for
/// explicit control and for the dev hot-reload path.
namespace wxl::lua
{
    /**
     * @brief Creates the state, opens base libraries, builds the `wxl` API and loads extensions.
     * @return true when the state is up (already-running is treated as success).
     */
    bool Start();

    /** @brief Detaches the event bridge and closes the state. Safe to call when not running. */
    void Stop();

    /** @brief The live lua_State, or null when the VM is not running. */
    lua_State* State();

    /** @brief Current LuaJIT GC footprint in bytes (0 when not running). */
    size_t MemoryBytes();

    /**
     * @brief Per-frame pump, called on the game thread. Drives dev hot-reload today and is the slot
     *        for future timer/coroutine servicing. Not a hook itself — the engine subscribes it to
     *        the per-frame event; it never installs a detour of its own.
     */
    void OnFrame();
}
