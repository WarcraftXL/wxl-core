// GUID-backed object proxies: the userdata + metatable machinery behind wxl Unit objects.
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

#include "engine/lua/ObjectProxy.hpp"

#include "engine/lua/LuaJit.hpp"
#include "game/Binding.hpp"
#include "game/unit/Unit.hpp"
#include "offsets/game/Unit.hpp"

#include <windows.h>

#include <cstdio>

namespace wxl::lua
{
    namespace
    {
        namespace off = wxl::offsets::game::unit;

        // Registry key of the weak-valued cache table {guidBytes -> Unit userdata} that interns
        // proxy identity. Keyed by the GUID's raw 8 bytes because a Lua number (double) cannot hold
        // a full u64 losslessly.
        constexpr char kProxyCacheKey[] = "wxl.proxycache";

        // --- metamethods ---

        // Equality by GUID. Lua 5.1 only invokes __eq when both operands share this metatable, so
        // both userdata are known-good Units here.
        int L_eq(lua_State* L)
        {
            const auto* a = static_cast<ObjectProxy*>(luaL_checkudata(L, 1, kUnitMeta));
            const auto* b = static_cast<ObjectProxy*>(luaL_checkudata(L, 2, kUnitMeta));
            lua_pushboolean(L, a->guid == b->guid);
            return 1;
        }

        // "Unit:0x<16 hex>". lua_pushfstring lacks a 64-bit integer format, so format by hand.
        int L_tostring(lua_State* L)
        {
            const auto* p = static_cast<ObjectProxy*>(luaL_checkudata(L, 1, kUnitMeta));
            char buf[32];
            std::snprintf(buf, sizeof(buf), "Unit:0x%016llX",
                          static_cast<unsigned long long>(p->guid));
            lua_pushstring(L, buf);
            return 1;
        }
    } // namespace

    void RegisterMetatables(lua_State* L)
    {
        // Weak-valued cache: a proxy that Lua no longer references is collected and drops out.
        lua_newtable(L);                        // [cache]
        lua_newtable(L);                        // [cache, mt]
        lua_pushstring(L, "v");
        lua_setfield(L, -2, "__mode");          // weak values
        lua_setmetatable(L, -2);                // [cache]
        lua_setfield(L, LUA_REGISTRYINDEX, kProxyCacheKey);

        // Unit metatable: identity/printing metamethods, a locked __metatable, and a method table
        // under __index that the Unit method context fills in later at Register().
        luaL_newmetatable(L, kUnitMeta);        // [mt]
        lua_pushcfunction(L, &L_eq);
        lua_setfield(L, -2, "__eq");
        lua_pushcfunction(L, &L_tostring);
        lua_setfield(L, -2, "__tostring");
        lua_pushboolean(L, 0);
        lua_setfield(L, -2, "__metatable");     // getmetatable() from Lua sees false, not the table
        lua_newtable(L);                        // [mt, methods]
        lua_setfield(L, -2, "__index");         // mt.__index = methods
        lua_pop(L, 1);                          // []
    }

    void PushUnit(lua_State* L, uint64_t guid)
    {
        if (guid == 0)
        {
            lua_pushnil(L);
            return;
        }

        lua_getfield(L, LUA_REGISTRYINDEX, kProxyCacheKey);   // [cache]
        lua_pushlstring(L, reinterpret_cast<const char*>(&guid), sizeof(guid)); // [cache, key]
        lua_pushvalue(L, -1);                                 // [cache, key, key]
        lua_rawget(L, -3);                                    // [cache, key, cached|nil]
        if (!lua_isnil(L, -1))
        {
            lua_replace(L, -3);                               // [cached, key]
            lua_pop(L, 1);                                    // [cached]
            return;
        }
        lua_pop(L, 1);                                        // [cache, key]

        auto* p  = static_cast<ObjectProxy*>(lua_newuserdata(L, sizeof(ObjectProxy))); // [cache, key, ud]
        p->guid  = guid;
        p->type  = ObjectType::Unit;
        luaL_getmetatable(L, kUnitMeta);                      // [cache, key, ud, mt]
        lua_setmetatable(L, -2);                              // [cache, key, ud]

        lua_pushvalue(L, -2);                                 // [cache, key, ud, key]
        lua_pushvalue(L, -2);                                 // [cache, key, ud, key, ud]
        lua_rawset(L, -5);                                    // cache[key] = ud -> [cache, key, ud]
        lua_replace(L, -3);                                   // [ud, key]
        lua_pop(L, 1);                                        // [ud]
    }

    uint64_t CheckGuid(lua_State* L, int idx)
    {
        void* ud = luaL_checkudata(L, idx, kUnitMeta);
        return static_cast<ObjectProxy*>(ud)->guid;
    }

    void* ResolveObject(uint64_t guid, uint32_t typemask)
    {
        if (guid == 0)
            return nullptr;
        __try
        {
            return wxl::game::Native<off::GetObjectFn>(off::kGetObjectByGuid)(guid, typemask, "wxl", 0);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    void* ResolveUnit(uint64_t guid)
    {
        return ResolveObject(guid, off::kTypeMaskUnit | off::kTypeMaskPlayer);
    }

    uint64_t ReadGuidAt(uintptr_t addr)
    {
        __try
        {
            return *reinterpret_cast<const uint64_t*>(addr);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    uint64_t ActivePlayerGuid()
    {
        __try
        {
            return wxl::game::Native<off::ActivePlayerGuidFn>(off::kActivePlayerGuid)();
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    bool ReadPosition(void* unit, float out[3])
    {
        if (!unit)
            return false;
        __try
        {
            const auto* u = static_cast<off::UnitObject*>(unit);
            out[0] = u->position[0];
            out[1] = u->position[1];
            out[2] = u->position[2];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool ReadReaction(void* self, void* other, int* out)
    {
        if (!self || !other)
            return false;
        __try
        {
            *out = wxl::game::unit::Reaction(self, other);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
}
