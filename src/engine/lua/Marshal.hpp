// Stack marshalling primitives: the Push/Check/Opt helpers the whole Lua surface is built on.
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

#include "engine/lua/LuaJit.hpp"

#include <cstdarg>
#include <string_view>

/// The narrow, typed seam between the Lua stack and C++. Every value that crosses the boundary
/// crosses through here: the objects an event pushes to a handler (Push overloads, and ObjectProxy
/// on top of them) and the arguments a method reads back (the Check/Opt family). Keeping the
/// conversions in one header means one place decides how a bool, an integer, a number or a string
/// is represented, and one place raises a typed error — so a method body stays free of raw lua_*
/// calls and reads as intent. Header-only and inline: pure declaration, no separate TU.
namespace wxl::lua
{
    // --- Push: C++ value -> Lua stack (one value pushed each) ---
    inline void Push(lua_State* L, bool v)             { lua_pushboolean(L, v ? 1 : 0); }
    inline void Push(lua_State* L, lua_Integer v)      { lua_pushinteger(L, v); }
    inline void Push(lua_State* L, double v)           { lua_pushnumber(L, v); }
    inline void Push(lua_State* L, const char* v)      { lua_pushstring(L, v ? v : ""); }
    inline void Push(lua_State* L, std::string_view v) { lua_pushlstring(L, v.data(), v.size()); }
    inline void PushNil(lua_State* L)                  { lua_pushnil(L); }

    // --- Check: required argument, raises a typed Lua error on mismatch ---
    inline bool CheckBool(lua_State* L, int idx)
    { luaL_checktype(L, idx, LUA_TBOOLEAN); return lua_toboolean(L, idx) != 0; }
    inline lua_Integer CheckInt(lua_State* L, int idx)    { return luaL_checkinteger(L, idx); }
    inline double      CheckNumber(lua_State* L, int idx) { return luaL_checknumber(L, idx); }
    inline const char* CheckString(lua_State* L, int idx) { return luaL_checkstring(L, idx); }

    // --- Opt: optional argument, falls back when the slot is none-or-nil ---
    inline lua_Integer OptInt(lua_State* L, int idx, lua_Integer fallback)
    { return luaL_optinteger(L, idx, fallback); }
    inline double OptNumber(lua_State* L, int idx, double fallback)
    { return luaL_optnumber(L, idx, fallback); }
    inline const char* OptString(lua_State* L, int idx, const char* fallback)
    { return luaL_optstring(L, idx, fallback); }

    /**
     * @brief Raises a formatted Lua error (source position prefixed, then longjmp) — never returns.
     * @param L    the Lua state.
     * @param fmt  a lua_pushvfstring format (%s, %d, %f, %p, %c, %%); NOT a full printf.
     * @return declared int so a method body can `return Error(...)`, though control never returns.
     */
    inline int Error(lua_State* L, const char* fmt, ...)
    {
        va_list ap;
        va_start(ap, fmt);
        luaL_where(L, 1);              // "chunk:line: " prefix, like luaL_error
        lua_pushvfstring(L, fmt, ap);
        va_end(ap);
        lua_concat(L, 2);
        return lua_error(L);
    }

    /**
     * @brief Sets a null-terminated {name, fn} array into the table on top of the stack. Stack-neutral.
     *
     * The one loop that replaces a column of lua_pushcfunction / lua_setfield pairs: a context lists its
     * functions once, in an array that reads as a table of contents. LuaJIT's luaL_register(L, NULL, ...)
     * writes each entry into the table already on top (no global pollution).
     * @param L    the Lua state, with the target table at index -1.
     * @param fns  null-terminated array of {name, lua_CFunction}.
     */
    inline void SetFunctions(lua_State* L, const luaL_Reg* fns)
    {
        luaL_register(L, nullptr, fns);
    }

    /**
     * @brief Creates a fresh subtable, fills it from fns, and stores it as field `name` of the table on
     *        top of the stack. Stack-neutral (the parent table stays on top). The `wxl.<ctx>` idiom.
     * @param L     the Lua state, with the parent table (e.g. `wxl`) at index -1.
     * @param name  the field name the subtable is stored under (e.g. "sound").
     * @param fns   null-terminated array of {name, lua_CFunction}.
     */
    inline void RegisterModule(lua_State* L, const char* name, const luaL_Reg* fns)
    {
        lua_newtable(L);              // [parent, module]
        luaL_register(L, nullptr, fns);
        lua_setfield(L, -2, name);    // [parent]
    }

    /**
     * @brief Creates (or fetches) a named metatable in the registry and populates its metamethods.
     *        The metatable is left popped; look it up later with luaL_getmetatable(L, name).
     * @param L        the Lua state.
     * @param name     registry key / type name (e.g. "wxl.Unit").
     * @param metafns  null-terminated array of metamethods, or nullptr for an empty metatable.
     */
    inline void RegisterMetatable(lua_State* L, const char* name, const luaL_Reg* metafns)
    {
        luaL_newmetatable(L, name);           // [mt]
        if (metafns)
            luaL_register(L, nullptr, metafns); // set metafns into the table on top
        lua_pop(L, 1);                        // []
    }
}
