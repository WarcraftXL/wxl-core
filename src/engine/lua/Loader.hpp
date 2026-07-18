// Extension discovery and loading: finds <exe>/extensions and runs each .lua / .out in order.
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

/// Discovers and loads the client's extensions. The directory is `<Wow.exe dir>/extensions`,
/// overridable by the WXL_EXTENSIONS_DIR knob (env or WarcraftXL.cfg). Files are loaded in a
/// deterministic lexicographic order; one file's failure is logged and isolated so the others
/// still load. All calls run on the game thread (the engine owns thread affinity).
namespace wxl::lua::loader
{
    /**
     * @brief Resolves the extensions directory into buf (no trailing separator).
     * @param buf  receives the NUL-terminated path.
     * @param cap  buffer capacity.
     * @return true when a path was written.
     */
    bool ResolveDir(char* buf, size_t cap);

    /**
     * @brief Loads and runs the extensions directory's *.lua / *.out, gated by the signed manifest.
     *
     * Production: `<extDir>/manifest` (+ .sig) must verify against the DLL's embedded Ed25519 key or
     * NOTHING loads (fail closed, docs/plan-v1.1.md §4). Each candidate is then hash-checked against
     * its manifest entry; a file that is out-of-manifest, tampered, or (for compiled .out) built for
     * a different LuaJIT is warned and skipped. Dev mode (WXL_DEV_MODE) bypasses every check.
     * @param L  the engine lua_State.
     * @return the number of files that loaded and ran without error.
     */
    int LoadAll(lua_State* L);
}
