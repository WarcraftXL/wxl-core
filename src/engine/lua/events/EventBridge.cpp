// Event bridge: the common wxl::events -> Lua callback machinery behind wxl.on(name, fn).
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

#include "engine/lua/events/EventBridge.hpp"

#include "engine/lua/LuaJit.hpp"
#include "common/Log.hpp"

#include <cstdint>
#include <string.h>
#include <vector>

namespace wxl::lua::events
{
    namespace
    {
        /// One declared event: its Lua name, the bus event, its arg marshaller, and the live list of
        /// registered Lua callbacks (luaL_ref handles into the current state's registry).
        struct Entry
        {
            const char*        name;
            wxl::events::Event ev;
            PushFn             push;
            std::vector<int>   refs;
        };

        // Declared once at install and thereafter stable (never resized during dispatch), so an
        // index is a safe opaque handle to pass through the bus as `user`. Only `refs` mutates.
        std::vector<Entry> g_entries;
        lua_State*         g_L         = nullptr;
        bool               g_subscribed = false;

        /// Bus handler shared by every declared event; `user` is the g_entries index.
        void Dispatch(void* user, const void* args)
        {
            if (!g_L)
                return;
            Entry&     e = g_entries[reinterpret_cast<size_t>(user)];
            lua_State* L = g_L;

            for (size_t i = 0; i < e.refs.size();)
            {
                const int base = lua_gettop(L);
                lua_rawgeti(L, LUA_REGISTRYINDEX, e.refs[i]); // push the handler
                const int nargs = e.push ? e.push(L, args) : 0;
                if (lua_pcall(L, nargs, 0, 0) != 0)
                {
                    const char* msg = lua_tostring(L, -1);
                    WLOG_WARN("[ext] event '%s' handler error: %s", e.name, msg ? msg : "?");
                    lua_settop(L, base);
                    luaL_unref(L, LUA_REGISTRYINDEX, e.refs[i]);
                    e.refs.erase(e.refs.begin() + static_cast<long>(i));
                }
                else
                {
                    lua_settop(L, base);
                    ++i;
                }
            }
        }

        /// wxl.on(eventName, fn): pins the function against GC and appends it to the event's list.
        int L_on(lua_State* L)
        {
            const char* name = luaL_checkstring(L, 1);
            luaL_checktype(L, 2, LUA_TFUNCTION);
            for (size_t i = 0; i < g_entries.size(); ++i)
            {
                if (std::strcmp(g_entries[i].name, name) == 0)
                {
                    lua_pushvalue(L, 2);
                    g_entries[i].refs.push_back(luaL_ref(L, LUA_REGISTRYINDEX));
                    return 0;
                }
            }
            return luaL_error(L, "wxl.on: unknown event '%s'", name);
        }
    } // namespace

    void Declare(const char* name, wxl::events::Event ev, PushFn push)
    {
        g_entries.push_back(Entry{ name, ev, push, {} });
    }

    void SubscribeBus()
    {
        if (g_subscribed)
            return;
        for (size_t i = 0; i < g_entries.size(); ++i)
            wxl::events::Subscribe(g_entries[i].ev, &Dispatch, reinterpret_cast<void*>(i));
        g_subscribed = true;
    }

    void Bind(lua_State* L)
    {
        g_L = L;
        for (Entry& e : g_entries)
            e.refs.clear(); // a prior state (if any) is being torn down; its registry is freed by lua_close
        lua_pushcfunction(L, &L_on);
        lua_setfield(L, -2, "on"); // add wxl.on to the table on top of the stack
    }

    void Detach()
    {
        for (Entry& e : g_entries)
            e.refs.clear();
        g_L = nullptr;
    }
}
