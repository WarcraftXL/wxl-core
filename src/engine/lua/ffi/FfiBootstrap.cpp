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

#include "engine/lua/ffi/FfiBootstrap.hpp"

#include "engine/lua/ffi/CdefGen.hpp"
#include "engine/lua/LuaJit.hpp"
#include "offsets/game/Unit.hpp"
#include "common/Log.hpp"

#include <cstddef>
#include <string>

namespace wxl::lua::ffi
{
    namespace
    {
        namespace off = ::wxl::offsets::game::unit;

        const char* ErrStr(lua_State* L)
        {
            const char* s = lua_tostring(L, -1);
            return s ? s : "?";
        }

        /// The Lua-side "static_assert": evaluate ffi.offsetof(name, field) and ffi.sizeof(name)
        /// and compare against the C++ constants. This is the green/red gate for one struct.
        /// `ffiIdx` is the absolute stack index of the ffi table. Stack-neutral.
        void CheckStruct(lua_State* L, int ffiIdx, const char* name, const char* field,
                         size_t expectOff, size_t expectSize)
        {
            const int top = lua_gettop(L);

            lua_getfield(L, ffiIdx, "offsetof");
            lua_pushstring(L, name);
            lua_pushstring(L, field);
            if (lua_pcall(L, 2, 1, 0) != 0)
            {
                WLOG_ERROR("[vm] ffi: offsetof(%s, %s) failed: %s", name, field, ErrStr(L));
                lua_settop(L, top);
                return;
            }
            const size_t gotOff = static_cast<size_t>(lua_tointeger(L, -1));
            lua_pop(L, 1);

            lua_getfield(L, ffiIdx, "sizeof");
            lua_pushstring(L, name);
            if (lua_pcall(L, 1, 1, 0) != 0)
            {
                WLOG_ERROR("[vm] ffi: sizeof(%s) failed: %s", name, ErrStr(L));
                lua_settop(L, top);
                return;
            }
            const size_t gotSize = static_cast<size_t>(lua_tointeger(L, -1));
            lua_pop(L, 1);

            if (gotOff != expectOff || gotSize != expectSize)
            {
                WLOG_ERROR("[vm] ffi: %s layout MISMATCH - lua %s@0x%X sizeof %u; C++ 0x%X / %u",
                           name, field, static_cast<unsigned>(gotOff), static_cast<unsigned>(gotSize),
                           static_cast<unsigned>(expectOff), static_cast<unsigned>(expectSize));
                lua_settop(L, top);
                return;
            }
            WLOG_DEBUG("[vm] ffi: %s layout matches C++ (%s@0x%X, sizeof %u)",
                       name, field, static_cast<unsigned>(expectOff), static_cast<unsigned>(expectSize));
            lua_settop(L, top);
        }
    } // namespace

    void InstallFfi(lua_State* L)
    {
        const int base = lua_gettop(L);

        // require('ffi'), guarded: a LuaJIT built without FFI (LUAJIT_DISABLE_FFI) has no such
        // module, so a failed require means "no FFI" — warn and no-op rather than crash.
        lua_getglobal(L, "require");
        lua_pushstring(L, "ffi");
        if (lua_pcall(L, 1, 1, 0) != 0)
        {
            WLOG_WARN("[vm] ffi: require('ffi') failed (%s); cdefs not installed", ErrStr(L));
            lua_settop(L, base);
            return;
        }
        if (!lua_istable(L, -1))
        {
            WLOG_WARN("[vm] ffi: 'ffi' is not a table; cdefs not installed");
            lua_settop(L, base);
            return;
        }
        const int ffiIdx = lua_gettop(L);

        // ffi.cdef(BuildCoreCdefs()): register the type declarations generated from the offsets.
        const std::string cdefs = BuildCoreCdefs();
        WLOG_DEBUG("[vm] ffi: cdef block:\n%s", cdefs.c_str());

        lua_getfield(L, ffiIdx, "cdef");
        lua_pushlstring(L, cdefs.data(), cdefs.size());
        if (lua_pcall(L, 1, 0, 0) != 0)
        {
            WLOG_ERROR("[vm] ffi: cdef failed: %s", ErrStr(L));
            lua_settop(L, base);
            return;
        }

        // Self-check every registered struct against its canonical C++ view. The expected size is
        // sizeof(the C++ struct), which ends at the same last field, so this ties both sides to the
        // one set of constants.
        CheckStruct(L, ffiIdx, "wxl_Unit", "position", off::kUnitPositionField, sizeof(off::UnitObject));
        CheckStruct(L, ffiIdx, "wxl_Model", "parent", off::kModelParentField, sizeof(off::ModelObject));

        lua_settop(L, base);

        // SECURITY MODEL (plan-v1.1.md §3/§4): this runs in the engine's privileged init only. The
        // cdefs above are just type declarations, safe to register. We deliberately do NOT add any
        // wxl.* field that hands a raw pointer, an ffi handle, or ffi.cast to extension code —
        // untrusted-script FFI access is gated behind signing (milestone M2), which is not built yet.
    }
}
