// Hook: a typed detour on a named hook point or on an address. Holds the next link in the chain, so
// the detour and the function it passes through to share one type and a mismatch does not compile.
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

#include <cstdint>
#include <type_traits>

#include "wxl/Script.hpp"

namespace wxl
{
    /**
     * A detour and the chain link behind it, under one function type.
     *
     * @param Fn : the hooked function's type, either written out as `void __cdecl(void*)` or taken
     *             from a function-pointer alias such as `offsets::game::adt::Map_ChunkBuildFn`
     *
     * One instance owns one detour. A chain is never taken apart, so there is no detach: an instance
     * lives as long as the extension, which in practice means a namespace-scope or static object.
     *
     *     static wxl::Hook<adt::Map_ChunkBuildFn> g_chunkBuild;
     *
     *     void __fastcall hkChunkBuild(void* chunk, void* edx, void* raw, int flag)
     *     {
     *         g_chunkBuild(chunk, edx, raw, flag);
     *     }
     *
     *     g_chunkBuild.Attach("Adt.ChunkBuild", &hkChunkBuild);
     *
     * Naming the offsets alias rather than retyping the signature keeps one declaration of it: a
     * change to the engine prototype then fails to compile at the detour instead of passing the
     * wrong arguments at runtime.
     */
    template <class Fn>
    class Hook
    {
    public:
        /// The hooked function's type, with the pointer stripped when Fn was an alias for one.
        using Function = std::remove_pointer_t<Fn>;

        Hook() = default;

        /**
         * Installs the detour on a hook point the core knows by name.
         *
         * @param string pointName : a name from the core's hook-point table
         * @param Function* detour : runs in place of the engine function
         * @param int32 priority = WXL_HOOK_DEFAULT_PRIORITY : chain position, lower runs first
         * @return bool ok : false before ScriptMgr::Bind, or when the name is not a hook point
         */
        bool Attach(const char* pointName, Function* detour, int priority = WXL_HOOK_DEFAULT_PRIORITY)
        {
            const WXL_Api* api = ScriptMgr::Api();
            if (!api) return false;
            return api->HookAttachByName(pointName, reinterpret_cast<void*>(detour),
                                         reinterpret_cast<void**>(&original_), priority) != 0;
        }

        /**
         * Installs the detour on an address the caller resolved itself.
         *
         * @param string label : name this detour logs under
         * @param uintptr target : the address to detour
         * @param Function* detour : runs in place of the engine function
         * @param int32 priority = WXL_HOOK_DEFAULT_PRIORITY : chain position, lower runs first
         * @return bool ok : false before ScriptMgr::Bind
         */
        bool Attach(const char* label, uintptr_t target, Function* detour,
                    int priority = WXL_HOOK_DEFAULT_PRIORITY)
        {
            const WXL_Api* api = ScriptMgr::Api();
            if (!api) return false;
            return api->HookAttach(label, target, reinterpret_cast<void*>(detour),
                                   reinterpret_cast<void**>(&original_), priority) != 0;
        }

        /// True once the detour is installed and the chain link is known.
        explicit operator bool() const { return original_ != nullptr; }

        /**
         * The next link in the chain, which ends at the engine function.
         *
         * @return Function* original : null until Attach succeeds
         */
        Function* Original() const { return original_; }

        /**
         * Calls the next link in the chain. Not calling it suppresses both the parties behind this
         * detour and the engine function.
         *
         * @param ... : the hooked function's own arguments
         * @return : whatever the hooked function returns
         *
         * Only valid once Attach has succeeded, which is the case inside the detour itself.
         */
        template <class... Args>
        decltype(auto) operator()(Args&&... args) const
        {
            return original_(static_cast<Args&&>(args)...);
        }

    private:
        Function* original_ = nullptr;
    };
}
