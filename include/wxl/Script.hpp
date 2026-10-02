// The script layer: derive a script type, override the hooks you need, register the script in
// AddScripts(). Each type's hooks are the rows of its table under wxl/scripts/.
//
//   #include "wxl/Script.hpp"
//   class Hello final : public wxl::WorldScript
//   {
//       void OnWorldEnter(uint32_t mapId) override { wxl::ScriptMgr::Log(WXL_LOG_INFO, "map %u", mapId); }
//   };
//   WXL_DECLARE_EXTENSION("wxl-hello", 1)
//   void AddScripts() { wxl::ScriptMgr::Add(new Hello()); }
//
// A class may derive several types at once. A script receives every hook of its type; the empty
// default of a hook costs one virtual call. Scripts live for the process: nothing deletes them.
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

#include "wxl/Common.hpp"
#include "wxl/Events.hpp"
#include "wxl/PluginApi.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>

namespace wxl
{
    class ScriptMgr;

    /// The base of every script type: the name, and the hooks the types register at construction.
    class ScriptObject
    {
    public:
        virtual ~ScriptObject() = default;

        /**
         * The script's name, for the log; empty unless the script set one.
         *
         * @return string name
         */
        const char* GetName() const { return name_; }

    protected:
        ScriptObject() = default;

        /**
         * Names the script in the log lines ScriptMgr writes about it.
         *
         * @param string name : a literal or storage that outlives the script
         */
        void SetName(const char* name) { name_ = name ? name : ""; }

        /// One hook a type registered: the event, the trampoline and the subobject it dispatches to.
        struct Binding { events::Event event; events::Handler handler; void* user; };

        /// Records a hook; ScriptMgr::Add subscribes the lot. A type registers each hook once.
        void Register(events::Event event, events::Handler handler, void* user)
        {
            if (count_ < kMaxBindings) bindings_[count_++] = { event, handler, user };
        }

    private:
        friend class ScriptMgr;
        static constexpr uint32_t kMaxBindings = static_cast<uint32_t>(events::Event::Count);

        const char* name_ = "";
        Binding     bindings_[kMaxBindings] = {};
        uint32_t    count_ = 0;
    };

    /// The extension's registry: holds the core's service table and subscribes every added script.
    class ScriptMgr
    {
    public:
        /**
         * Records the service table WXL_Load received and the extension's name. WXL_DECLARE_EXTENSION calls
         * it; a hand-written WXL_Load calls it before AddScripts().
         *
         * @param WXL_Api api : the table, kept for the process lifetime
         * @param string name : the extension's name, the tag of its log lines
         * @return bool ok : false when the table is null or of another API version
         */
        static bool Bind(const WXL_Api* api, const char* name)
        {
            if (!api || api->apiVersion != WXL_API_VERSION) return false;
            State().api  = api;
            State().name = name ? name : "extension";
            for (uint32_t i = 0; i < State().pendingCount; ++i) Subscribe(State().pending[i]);
            State().pendingCount = 0;
            return true;
        }

        /**
         * Registers a script: every hook of its types is subscribed. Before Bind, the script waits
         * and is subscribed by Bind.
         *
         * @param ScriptObject script : owned by the process from now on
         */
        static void Add(ScriptObject* script)
        {
            if (!script) return;
            if (State().api) { Subscribe(script); return; }
            if (State().pendingCount < kMaxPending) State().pending[State().pendingCount++] = script;
        }

        /**
         * The core's service table, null before Bind.
         *
         * @return WXL_Api api
         */
        static const WXL_Api* Api() { return State().api; }

        /**
         * The extension's name as given to Bind.
         *
         * @return string name
         */
        static const char* Name() { return State().name; }

        /**
         * Writes one line to the core's log under the extension's name.
         *
         * @param int32 level : a WXL_LOG_* value
         * @param string fmt : printf format
         */
        static void Log(int level, const char* fmt, ...)
        {
            if (!State().api) return;
            char line[1024];
            va_list args;
            va_start(args, fmt);
            std::vsnprintf(line, sizeof line, fmt, args);
            va_end(args);
            State().api->Log(level, State().name, "%s", line);
        }

        /**
         * A service another extension published, typed.
         *
         * @param string name : the agreed service name
         * @param uint32 version : the agreed service version
         * @return T* service : null when nobody published it
         */
        template <class T>
        static T* GetInterface(const char* name, uint32_t version)
        {
            return State().api ? static_cast<T*>(State().api->GetInterface(name, version)) : nullptr;
        }

    private:
        static constexpr uint32_t kMaxPending = 64;

        struct Data
        {
            const WXL_Api* api  = nullptr;
            const char*    name = "extension";
            ScriptObject*  pending[kMaxPending] = {};
            uint32_t       pendingCount = 0;
        };
        static Data& State() { static Data data; return data; }

        static void Subscribe(ScriptObject* script)
        {
            for (uint32_t i = 0; i < script->count_; ++i)
            {
                const ScriptObject::Binding& b = script->bindings_[i];
                State().api->Subscribe(static_cast<uint32_t>(b.event), b.handler, b.user);
            }
        }
    };

    /**
     * The world, the frame tick, input and the player's target. Hooks: wxl/scripts/WorldScript.def.
     */
    class WorldScript : public virtual ScriptObject
    {
    public:
        WorldScript()
        {
#define WXL_SCRIPT_HOOK(name, params, args) \
            Register(events::Event::name, &WorldScript::Dispatch_##name, static_cast<WorldScript*>(this));
#include "wxl/scripts/WorldScript.def"
#undef WXL_SCRIPT_HOOK
        }

#define WXL_SCRIPT_HOOK(name, params, args) virtual void name params {}
#include "wxl/scripts/WorldScript.def"
#undef WXL_SCRIPT_HOOK

    private:
#define WXL_SCRIPT_HOOK(name, params, args)                                                   \
        static void WXL_CDECL Dispatch_##name(void* user, const void* raw)                   \
        {                                                                                     \
            const auto& a = *static_cast<const events::Args<events::Event::name>*>(raw);     \
            static_cast<WorldScript*>(user)->name args;                                              \
        }
#include "wxl/scripts/WorldScript.def"
#undef WXL_SCRIPT_HOOK
    };

    /**
     * The device, the frame and the world passes. Hooks: wxl/scripts/RenderScript.def.
     */
    class RenderScript : public virtual ScriptObject
    {
    public:
        RenderScript()
        {
#define WXL_SCRIPT_HOOK(name, params, args) \
            Register(events::Event::name, &RenderScript::Dispatch_##name, static_cast<RenderScript*>(this));
#include "wxl/scripts/RenderScript.def"
#undef WXL_SCRIPT_HOOK
        }

#define WXL_SCRIPT_HOOK(name, params, args) virtual void name params {}
#include "wxl/scripts/RenderScript.def"
#undef WXL_SCRIPT_HOOK

    private:
#define WXL_SCRIPT_HOOK(name, params, args)                                                   \
        static void WXL_CDECL Dispatch_##name(void* user, const void* raw)                   \
        {                                                                                     \
            const auto& a = *static_cast<const events::Args<events::Event::name>*>(raw);     \
            static_cast<RenderScript*>(user)->name args;                                              \
        }
#include "wxl/scripts/RenderScript.def"
#undef WXL_SCRIPT_HOOK
    };

    /**
     * M2 models, skins, poses and batches; several hooks fire per instance or per batch. Hooks: wxl/scripts/ModelScript.def.
     */
    class ModelScript : public virtual ScriptObject
    {
    public:
        ModelScript()
        {
#define WXL_SCRIPT_HOOK(name, params, args) \
            Register(events::Event::name, &ModelScript::Dispatch_##name, static_cast<ModelScript*>(this));
#include "wxl/scripts/ModelScript.def"
#undef WXL_SCRIPT_HOOK
        }

#define WXL_SCRIPT_HOOK(name, params, args) virtual void name params {}
#include "wxl/scripts/ModelScript.def"
#undef WXL_SCRIPT_HOOK

    private:
#define WXL_SCRIPT_HOOK(name, params, args)                                                   \
        static void WXL_CDECL Dispatch_##name(void* user, const void* raw)                   \
        {                                                                                     \
            const auto& a = *static_cast<const events::Args<events::Event::name>*>(raw);     \
            static_cast<ModelScript*>(user)->name args;                                              \
        }
#include "wxl/scripts/ModelScript.def"
#undef WXL_SCRIPT_HOOK
    };

    /**
     * Server objects, doodads and character equipment. Hooks: wxl/scripts/ObjectScript.def.
     */
    class ObjectScript : public virtual ScriptObject
    {
    public:
        ObjectScript()
        {
#define WXL_SCRIPT_HOOK(name, params, args) \
            Register(events::Event::name, &ObjectScript::Dispatch_##name, static_cast<ObjectScript*>(this));
#include "wxl/scripts/ObjectScript.def"
#undef WXL_SCRIPT_HOOK
        }

#define WXL_SCRIPT_HOOK(name, params, args) virtual void name params {}
#include "wxl/scripts/ObjectScript.def"
#undef WXL_SCRIPT_HOOK

    private:
#define WXL_SCRIPT_HOOK(name, params, args)                                                   \
        static void WXL_CDECL Dispatch_##name(void* user, const void* raw)                   \
        {                                                                                     \
            const auto& a = *static_cast<const events::Args<events::Event::name>*>(raw);     \
            static_cast<ObjectScript*>(user)->name args;                                              \
        }
#include "wxl/scripts/ObjectScript.def"
#undef WXL_SCRIPT_HOOK
    };

    /**
     * Terrain, WMO and texture loads. Hooks: wxl/scripts/AssetScript.def.
     */
    class AssetScript : public virtual ScriptObject
    {
    public:
        AssetScript()
        {
#define WXL_SCRIPT_HOOK(name, params, args) \
            Register(events::Event::name, &AssetScript::Dispatch_##name, static_cast<AssetScript*>(this));
#include "wxl/scripts/AssetScript.def"
#undef WXL_SCRIPT_HOOK
        }

#define WXL_SCRIPT_HOOK(name, params, args) virtual void name params {}
#include "wxl/scripts/AssetScript.def"
#undef WXL_SCRIPT_HOOK

    private:
#define WXL_SCRIPT_HOOK(name, params, args)                                                   \
        static void WXL_CDECL Dispatch_##name(void* user, const void* raw)                   \
        {                                                                                     \
            const auto& a = *static_cast<const events::Args<events::Event::name>*>(raw);     \
            static_cast<AssetScript*>(user)->name args;                                              \
        }
#include "wxl/scripts/AssetScript.def"
#undef WXL_SCRIPT_HOOK
    };
}

/**
 * The extension's two entry points, written once: WXL_Query describes it, WXL_Load binds the
 * ScriptMgr and calls AddScripts(), which the extension defines.
 *
 * @param string name : the extension's name
 * @param uint32 version : the extension's own version
 */
#define WXL_DECLARE_EXTENSION(name, version)                                                         \
    void AddScripts();                                                                          \
    extern "C" WXL_EXPORT const WXL_PluginInfo* WXL_CDECL WXL_Query(void)                      \
    {                                                                                           \
        static const WXL_PluginInfo info = { sizeof(WXL_PluginInfo), WXL_API_VERSION, name,     \
                                             version, WXL_CLIENT_BUILD };                       \
        return &info;                                                                           \
    }                                                                                           \
    extern "C" WXL_EXPORT int WXL_CDECL WXL_Load(const WXL_Api* api)                            \
    {                                                                                           \
        if (!::wxl::ScriptMgr::Bind(api, name)) return 0;                                       \
        AddScripts();                                                                           \
        return 1;                                                                               \
    }
