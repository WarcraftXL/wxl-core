// Settings sets reachable from Lua by name: the WXL_Settings_* script functions (wxl/LuaApi.h).
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
// None of these functions raises: a bad set, key or value answers nil or false, so an options panel
// written against them fails softly whatever it is handed.

#include "engine/script/LuaSettings.hpp"

#include "engine/script/LuaRegistry.hpp"

#include "common/Log.hpp"
#include "wxl/game/Script.hpp"

#include <cstdio>
#include <cstring>

namespace
{
    namespace gs = wxl::game::script;

    constexpr int kMaxSets = 32;
    const WXL_SettingsSet* g_sets[kMaxSets] = {};
    int g_setCount = 0;

    /// The set the first argument names, or null.
    const WXL_SettingsSet* SetArg(void* L)
    {
        if (gs::ArgCount(L) < 1 || gs::Type(L, 1) != 4) return nullptr;
        const char* name = gs::ToString(L, 1);
        if (!name) return nullptr;
        for (int i = 0; i < g_setCount; ++i)
            if (std::strcmp(g_sets[i]->name, name) == 0) return g_sets[i];
        return nullptr;
    }

    void PushValue(void* L, const WXL_SettingValue& v)
    {
        switch (v.type)
        {
            case WXL_SETTING_BOOLEAN: gs::PushBoolean(L, v.boolean != 0); break;
            case WXL_SETTING_NUMBER:  gs::PushNumber(L, v.number); break;
            case WXL_SETTING_STRING:  gs::PushString(L, v.string ? v.string : ""); break;
            default:                  gs::PushNil(L); break;
        }
    }

    namespace lua = wxl::offsets::engine::lua;

    void NewTable(void* L, int arrayCount, int recordCount)
    {
        wxl::game::Native<lua::LuaCreateTableFn>(lua::kLuaCreateTable)(L, arrayCount, recordCount);
    }

    bool Available(const WXL_SettingsSet* s) { return !s->IsAvailable || s->IsAvailable(s->user) != 0; }

    int __cdecl LuaList(void* L)
    {
        NewTable(L, g_setCount, 0);
        for (int i = 0; i < g_setCount; ++i)
        {
            gs::PushNumber(L, i + 1);
            gs::PushString(L, g_sets[i]->name);
            gs::RawSet(L, -3);
        }
        return 1;
    }

    int __cdecl LuaIsAvailable(void* L)
    {
        const WXL_SettingsSet* s = SetArg(L);
        gs::PushBoolean(L, s && Available(s));
        return 1;
    }

    int __cdecl LuaGet(void* L)
    {
        const WXL_SettingsSet* s = SetArg(L);
        const char* key = s && gs::ArgCount(L) >= 2 && gs::Type(L, 2) == 4 ? gs::ToString(L, 2) : nullptr;
        WXL_SettingValue v = {};
        if (key && s->Get(s->user, key, &v)) PushValue(L, v);
        else gs::PushNil(L);
        return 1;
    }

    int __cdecl LuaSet(void* L)
    {
        const WXL_SettingsSet* s = SetArg(L);
        bool ok = false;
        if (s && s->Set && gs::ArgCount(L) >= 3 && gs::Type(L, 2) == 4)
        {
            char key[64] = {};
            std::snprintf(key, sizeof key, "%s", gs::ToString(L, 2));
            WXL_SettingValue v = {};
            char text[128] = {};
            switch (gs::Type(L, 3))
            {
                case 1: v.type = WXL_SETTING_BOOLEAN; v.boolean = gs::ToBoolean(L, 3) ? 1 : 0; break;
                case 3: v.type = WXL_SETTING_NUMBER;  v.number = gs::ToNumber(L, 3); break;
                case 4:
                    v.type = WXL_SETTING_STRING;
                    std::snprintf(text, sizeof text, "%s", gs::ToString(L, 3));
                    v.string = text;
                    break;
                default: v.type = WXL_SETTING_NIL; break;
            }
            ok = v.type != WXL_SETTING_NIL && s->Set(s->user, key, &v) != 0;
        }
        gs::PushBoolean(L, ok);
        return 1;
    }

    int __cdecl LuaGetAll(void* L)
    {
        const WXL_SettingsSet* s = SetArg(L);
        if (!s)
        {
            gs::PushNil(L);
            return 1;
        }
        NewTable(L, 0, static_cast<int>(s->keyCount));
        for (uint32_t i = 0; i < s->keyCount; ++i)
        {
            WXL_SettingValue v = {};
            if (!s->keys[i] || !s->Get(s->user, s->keys[i], &v)) continue;
            gs::PushString(L, s->keys[i]);
            PushValue(L, v);
            gs::RawSet(L, -3);
        }
        return 1;
    }

    int __cdecl LuaApplyPreset(void* L)
    {
        const WXL_SettingsSet* s = SetArg(L);
        bool ok = false;
        if (s && s->ApplyPreset && gs::ArgCount(L) >= 2 && gs::Type(L, 2) == 4)
        {
            char name[64] = {};
            std::snprintf(name, sizeof name, "%s", gs::ToString(L, 2));
            ok = s->ApplyPreset(s->user, name) != 0;
        }
        gs::PushBoolean(L, ok);
        return 1;
    }

    int __cdecl LuaSave(void* L)
    {
        const WXL_SettingsSet* s = SetArg(L);
        gs::PushBoolean(L, s && s->Save && s->Save(s->user) != 0);
        return 1;
    }
}

namespace wxl::script
{
    bool InstallSettingsFunctions()
    {
        return AddFunction("WXL_Settings_List", &LuaList) && AddFunction("WXL_Settings_IsAvailable", &LuaIsAvailable)
               && AddFunction("WXL_Settings_Get", &LuaGet) && AddFunction("WXL_Settings_Set", &LuaSet)
               && AddFunction("WXL_Settings_GetAll", &LuaGetAll)
               && AddFunction("WXL_Settings_ApplyPreset", &LuaApplyPreset) && AddFunction("WXL_Settings_Save", &LuaSave);
    }

    bool AddSettings(const WXL_SettingsSet* set)
    {
        if (!set || set->structSize < sizeof(WXL_SettingsSet) || !set->name || !*set->name || !set->Get) return false;
        for (int i = 0; i < g_setCount; ++i)
            if (std::strcmp(g_sets[i]->name, set->name) == 0)
            {
                g_sets[i] = set;
                return true;
            }
        if (g_setCount == kMaxSets) return false;
        g_sets[g_setCount++] = set;
        WLOG_INFO("lua: settings set \"%s\" registered (%u key(s))", set->name, set->keyCount);
        return true;
    }

    uint32_t SettingsCount() { return static_cast<uint32_t>(g_setCount); }
    const WXL_SettingsSet* SettingsAt(uint32_t index)
    { return index < static_cast<uint32_t>(g_setCount) ? g_sets[index] : nullptr; }
}
