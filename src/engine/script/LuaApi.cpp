// The Lua service as extensions see it (wxl/LuaApi.h): the published table and its install.
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
//
// Installed at boot, before the client's startup proceeds: the glue screen's interface loads long
// before the Normal phase would. Nothing here depends on the graphics device.

#include "engine/hook/Registry.hpp"
#include "engine/script/LuaRegistry.hpp"
#include "engine/script/LuaSettings.hpp"
#include "runtime/Extensions.hpp"

#include "common/Log.hpp"
#include "wxl/LuaApi.h"
#include "wxl/game/Script.hpp"

namespace
{
    namespace gs = wxl::game::script;
    namespace lua  = wxl::offsets::engine::lua;

    int __cdecl ApiRegisterFunction(const char* name, WXL_LuaFunction fn) { return wxl::script::AddFunction(name, fn) ? 1 : 0; }
    int __cdecl ApiRegisterSettings(const WXL_SettingsSet* set) { return wxl::script::AddSettings(set) ? 1 : 0; }

    int __cdecl ApiArgCount(void* s) { return gs::ArgCount(s); }
    int __cdecl ApiType(void* s, int i) { return gs::Type(s, i); }
    double __cdecl ApiToNumber(void* s, int i) { return gs::ToNumber(s, i); }
    const char* __cdecl ApiToString(void* s, int i) { return gs::ToString(s, i); }
    int __cdecl ApiToBoolean(void* s, int i) { return gs::ToBoolean(s, i) ? 1 : 0; }
    void __cdecl ApiPushNil(void* s) { gs::PushNil(s); }
    void __cdecl ApiPushNumber(void* s, double v) { gs::PushNumber(s, v); }
    void __cdecl ApiPushString(void* s, const char* v) { gs::PushString(s, v ? v : ""); }
    void __cdecl ApiPushBoolean(void* s, int v) { gs::PushBoolean(s, v != 0); }
    void __cdecl ApiNewTable(void* s) { wxl::game::Native<lua::LuaCreateTableFn>(lua::kLuaCreateTable)(s, 0, 0); }

    // t[key] = value on the table at the top: key and value pushed above it, then a raw set at -3.
    void __cdecl ApiSetFieldNumber(void* s, const char* key, double v)
    {
        gs::PushString(s, key);
        gs::PushNumber(s, v);
        gs::RawSet(s, -3);
    }
    void __cdecl ApiSetFieldString(void* s, const char* key, const char* v)
    {
        gs::PushString(s, key);
        gs::PushString(s, v ? v : "");
        gs::RawSet(s, -3);
    }
    void __cdecl ApiSetFieldBoolean(void* s, const char* key, int v)
    {
        gs::PushString(s, key);
        gs::PushBoolean(s, v != 0);
        gs::RawSet(s, -3);
    }
    void __cdecl ApiError(void* s, const char* message) { gs::Error(s, "%s", message ? message : "error"); }

    const WXL_LuaApi g_luaApi = {
        sizeof(WXL_LuaApi),
        WXL_LUA_API_VERSION,
        &ApiRegisterFunction,
        &ApiRegisterSettings,
        &ApiArgCount,
        &ApiType,
        &ApiToNumber,
        &ApiToString,
        &ApiToBoolean,
        &ApiPushNil,
        &ApiPushNumber,
        &ApiPushString,
        &ApiPushBoolean,
        &ApiNewTable,
        &ApiSetFieldNumber,
        &ApiSetFieldString,
        &ApiSetFieldBoolean,
        &ApiError,
    };

    bool InstallLuaService()
    {
        if (!wxl::script::InstallSeams() || !wxl::script::InstallSettingsFunctions())
        {
            WLOG_WARN("lua: %s is not published", WXL_LUA_API_NAME);
            return false;
        }
        wxl::runtime::extensions::PublishInterface(WXL_LUA_API_NAME, WXL_LUA_API_VERSION, const_cast<WXL_LuaApi*>(&g_luaApi));
        return true;
    }
}

WXL_REGISTER_FEATURE_PHASED("lua-service", true, InstallLuaService, ::wxl::hook::Phase::Boot)
