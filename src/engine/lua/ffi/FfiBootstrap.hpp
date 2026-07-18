// FFI bootstrap: engine-privileged install of the generated cdefs, with a Lua-side layout self-check.
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

struct lua_State;

/// Engine-internal FFI bootstrap. Registers the generated cdefs (CdefGen) into the VM's ffi and
/// self-verifies that the Lua-side struct layout matches the C++ offset constants — the M0 proof
/// that one struct is shared by C++ and Lua with green asserts on both sides.
namespace wxl::lua::ffi
{
    /**
     * @brief Installs the core cdefs into the engine VM and asserts their layout against C++.
     *
     * Runs in the ENGINE's privileged init only, after luaL_openlibs. If the VM was built without
     * FFI, require('ffi') fails, a warning is logged, and the call is a no-op (never crashes a
     * no-FFI build). No ffi handle, ffi.cast, or raw pointer is exposed to extension code here —
     * untrusted-script FFI access is gated behind signing (milestone M2), not built yet.
     * @param L  the engine lua_State, with the standard libraries already open.
     */
    void InstallFfi(lua_State* L);
}
