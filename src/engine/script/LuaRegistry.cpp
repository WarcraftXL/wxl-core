// The registry of extension script functions, and the two seams that put them into every context.
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
// FrameXML_CreateFrames ("Lua.InterfaceLoad") is the last call before the client loads an interface
// onto a fresh context -- the glue screen's and the game's, and again on every /reload -- so the
// registered functions go in just before it runs, and every OnLoad sees them. The callback check
// ("Lua.ValidateFunctionPointer") refuses any function outside Wow.exe with a fatal error; the detour
// lets the registered pointers through and hands every other one to the original, so the check keeps
// doing its job for the calls it was written for.

#include "engine/script/LuaRegistry.hpp"
#include "engine/script/Profiles.hpp"

#include "runtime/HookPoints.hpp"

#include "common/Log.hpp"
#include "wxl/game/Script.hpp"

#include <cstring>

namespace
{
    namespace gs = wxl::game::script;
    namespace lua  = wxl::offsets::engine::lua;

    constexpr int kMaxFunctions = 256;

    struct Entry
    {
        char            name[64];
        WXL_LuaFunction fn;
    };
    Entry g_entries[kMaxFunctions];
    int   g_count = 0;

    lua::ValidateFunctionPointerFn g_origValidate = nullptr;
    lua::FrameXMLCreateFramesFn    g_origLoad     = nullptr;
    bool                           g_hooked       = false;

    bool Registered(uintptr_t fn)
    {
        for (int i = 0; i < g_count; ++i)
            if (reinterpret_cast<uintptr_t>(g_entries[i].fn) == fn) return true;
        return false;
    }

    /// The callback check: ours pass, everything else is the original's to judge.
    void __cdecl hkValidate(uintptr_t function)
    {
        if (Registered(function)) return;
        g_origValidate(function);
    }

    /// The interface load: the functions go into the context first.
    int __cdecl hkLoad(const char* tocPath, const char* addOnName, void* md5Context, void* status)
    {
        // Every extension has registered its settings by the first interface load: WXL_PROFILE now.
        wxl::script::profiles::LoadStartupOnce();
        if (g_count && gs::Context())
        {
            for (int i = 0; i < g_count; ++i)
                gs::Register(g_entries[i].name, reinterpret_cast<gs::Function>(g_entries[i].fn));
            static bool said = false;
            if (!said)
            {
                said = true;
                WLOG_INFO("lua: %d extension function(s) registered ahead of the interface load (%s)", g_count,
                          tocPath ? tocPath : "?");
            }
        }
        return g_origLoad(tocPath, addOnName, md5Context, status);
    }
}

namespace wxl::script
{
    bool InstallSeams()
    {
        const bool validate = wxl::runtime::hookpoints::Attach("Lua.ValidateFunctionPointer", &hkValidate, &g_origValidate);
        const bool load     = wxl::runtime::hookpoints::Attach("Lua.InterfaceLoad", &hkLoad, &g_origLoad);
        g_hooked = validate && load;
        if (!g_hooked)
            WLOG_WARN("lua: the script seams could not be hooked (validate %s, interface load %s)",
                      validate ? "ok" : "failed", load ? "ok" : "failed");
        return g_hooked;
    }

    bool AddFunction(const char* name, WXL_LuaFunction fn)
    {
        if (!g_hooked || !name || !*name || !fn || std::strlen(name) >= sizeof g_entries[0].name) return false;
        Entry* e = nullptr;
        for (int i = 0; i < g_count && !e; ++i)
            if (std::strcmp(g_entries[i].name, name) == 0) e = &g_entries[i];
        if (!e)
        {
            if (g_count == kMaxFunctions) return false;
            e = &g_entries[g_count++];
            std::strcpy(e->name, name);
        }
        e->fn = fn;
        // A context already up (a late registration) gets it now; every later load gets it again.
        if (gs::Context()) gs::Register(e->name, reinterpret_cast<gs::Function>(fn));
        WLOG_INFO("lua: %s registered", e->name);
        return true;
    }
}
