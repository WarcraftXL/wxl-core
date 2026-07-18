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

#include "engine/lua/LuaEngine.hpp"

#include "engine/lua/LuaJit.hpp"
#include "engine/lua/Loader.hpp"
#include "engine/lua/DevReload.hpp"
#include "engine/lua/methods/CoreMethods.hpp"
#include "engine/lua/events/EventBridge.hpp"
#include "engine/lua/events/FrameEvents.hpp"
#include "engine/events/Event.hpp"
#include "engine/hook/Registry.hpp"
#include "common/Log.hpp"

#include <windows.h>

#include <cassert>

namespace wxl::lua
{
    namespace
    {
        lua_State* g_L      = nullptr;
        DWORD      g_thread = 0;

        // Debug-only affinity guard: everything touching the state must run on the thread that
        // created it. Latched at Start(); a zero latch (never started) is permitted.
        void AssertGameThread()
        {
            assert((g_thread == 0 || GetCurrentThreadId() == g_thread) &&
                   "wxl::lua accessed off the game thread");
        }

        // Bridge from the engine's per-frame event to the VM. The first frame on the render thread
        // is the correct, guaranteed-single-thread moment to create the state (the engine is up by
        // then); later frames pump. Subscribed exactly once by InstallLua, so a reload never doubles
        // the subscription (the event bus has no unsubscribe).
        void OnFrameEvent(void* /*user*/, const void* /*args*/)
        {
            if (!g_L)
            {
                Start();
                return;
            }
            OnFrame();
        }
    } // namespace

    // Named (not anonymous) so the global-scope registrar below can take its address. Runs once at
    // bootstrap: subscribes the frame pump and wires every event context onto the bus BEFORE any
    // frame — the bus range-iterates its subscribers, so no Subscribe may happen inside an Emit.
    bool InstallLua()
    {
        ::wxl::events::Subscribe(::wxl::events::Event::OnFrame, &OnFrameEvent, nullptr);

        events::frame::Declare(); // + future context Declare() calls, one line each
        events::SubscribeBus();

        WLOG_DEBUG("[vm] lua engine installed; VM boots on the first frame");
        return true;
    }

    bool Start()
    {
        if (g_L)
            return true;

        g_thread = GetCurrentThreadId();

        lua_State* L = luaL_newstate();
        if (!L)
        {
            WLOG_ERROR("[vm] luaL_newstate failed (out of memory)");
            return false;
        }
        luaL_openlibs(L);

        // Assemble the global `wxl` table: each context adds its fields to the table on top.
        lua_newtable(L);
        methods::core::Register(L);
        events::Bind(L); // adds wxl.on and binds the bridge to this state
        lua_setglobal(L, "wxl");

        g_L = L;

        loader::LoadAll(L);
        WLOG_DEBUG("[vm] started, gc footprint %zu bytes", MemoryBytes());

        if (dev::Enabled())
            dev::Snapshot(); // adopt the just-loaded folder state as the reload baseline
        return true;
    }

    void Stop()
    {
        if (!g_L)
            return;
        AssertGameThread();

        events::Detach(); // drop retained callbacks before the registry is freed
        lua_close(g_L);
        g_L = nullptr;
        WLOG_DEBUG("[vm] stopped");
    }

    lua_State* State()
    {
        return g_L;
    }

    size_t MemoryBytes()
    {
        if (!g_L)
            return 0;
        const int kb = lua_gc(g_L, LUA_GCCOUNT, 0);
        const int b  = lua_gc(g_L, LUA_GCCOUNTB, 0);
        return static_cast<size_t>(kb) * 1024u + static_cast<size_t>(b);
        // A hard quota (WXL_VM_BUDGET_MB, plan-v1.1.md §3) is not enforced yet; the reading is
        // logged at start/reload so the footprint is visible before the quota lands.
    }

    void OnFrame()
    {
        AssertGameThread();

        if (dev::Enabled() && dev::PollChanged())
        {
            WLOG_INFO("[vm] extensions changed on disk, reloading");
            Stop();
            Start();
            WLOG_DEBUG("[vm] reloaded, gc footprint %zu bytes", MemoryBytes());
        }
        // Future: service timers / coroutines here (still on the game thread).
    }
}

// The VM is always present (not a per-folder feature toggle), so it registers with a literal true.
// The registry unrolls at the end of bootstrap, after the engine, which is the required order.
WXL_REGISTER_FEATURE("lua", true, ::wxl::lua::InstallLua)
