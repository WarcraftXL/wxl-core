// Core method context: wxl.version, wxl.log/log_debug/log_warn, wxl.config.
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
#include "common/Config.hpp"
#include "common/Log.hpp"

/// One method context = one header exposing Register(lua_State*), which adds its fields to the
/// table already on top of the stack (the global `wxl` table the engine is assembling). The API
/// surface grows by ADDING a context header (UnitMethods, WorldMethods, …) and one Register call
/// in LuaEngine, never by widening an existing file. Header-only and inline so a context is pure
/// declaration with no separate TU.
namespace wxl::lua::methods::core
{
    /// The engine VM's advertised version. Single source for wxl.version.
    inline constexpr char kVersion[] = "1.1.0-dev";

    inline int L_log(lua_State* L)
    {
        WLOG_INFO("[ext] %s", luaL_optstring(L, 1, ""));
        return 0;
    }
    inline int L_logDebug(lua_State* L)
    {
        WLOG_DEBUG("[ext] %s", luaL_optstring(L, 1, ""));
        return 0;
    }
    inline int L_logWarn(lua_State* L)
    {
        WLOG_WARN("[ext] %s", luaL_optstring(L, 1, ""));
        return 0;
    }

    /// wxl.config(name, fallback) -> the WXL_* knob value (env then WarcraftXL.cfg), else fallback
    /// (or nil). Read-only; never writes config.
    inline int L_config(lua_State* L)
    {
        const char* name = luaL_checkstring(L, 1);
        char        buf[512];
        if (wxl::config::Raw(name, buf, sizeof(buf)))
            lua_pushstring(L, buf);
        else if (!lua_isnoneornil(L, 2))
            lua_pushvalue(L, 2);
        else
            lua_pushnil(L);
        return 1;
    }

    /**
     * @brief Adds the core fields to the table on top of the stack. Stack-neutral.
     * @param L  the engine lua_State, with the target `wxl` table at index -1.
     */
    inline void Register(lua_State* L)
    {
        lua_pushstring(L, kVersion);         lua_setfield(L, -2, "version");
        lua_pushcfunction(L, &L_log);        lua_setfield(L, -2, "log");
        lua_pushcfunction(L, &L_logDebug);   lua_setfield(L, -2, "log_debug");
        lua_pushcfunction(L, &L_logWarn);    lua_setfield(L, -2, "log_warn");
        lua_pushcfunction(L, &L_config);     lua_setfield(L, -2, "config");
    }
}
