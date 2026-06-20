// Named hooking over MinHook: install by name + address, enable in one batch.
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

#include "core/Hook.hpp"
#include "core/Logger.hpp"

#include <MinHook.h>

namespace wxl::core::hook
{
    /**
     * @brief Initialises the hooking engine once at startup.
     * @return true if initialisation succeeded.
     */
    bool Init()
    {
        MH_STATUS s = MH_Initialize();
        if (s != MH_OK)
        {
            WLOG_ERROR("hook: MH_Initialize failed (%s)", MH_StatusToString(s));
            return false;
        }
        return true;
    }

    /**
     * @brief Installs one detour.
     * @param name      label used for logging.
     * @param target    engine function address to detour.
     * @param detour    replacement function.
     * @param original  receives the trampoline to the original function.
     * @return true if the detour was created.
     */
    bool Install(const char* name, void* target, void* detour, void** original)
    {
        MH_STATUS s = MH_CreateHook(target, detour, original);
        if (s != MH_OK)
        {
            WLOG_ERROR("hook: create '%s' @0x%p failed (%s)", name, target, MH_StatusToString(s));
            return false;
        }
        WLOG_INFO("hook: installed '%s' @0x%p", name, target);
        return true;
    }

    /**
     * @brief Enables every installed hook.
     * @return true if all hooks were enabled.
     */
    bool EnableAll()
    {
        MH_STATUS s = MH_EnableHook(MH_ALL_HOOKS);
        if (s != MH_OK)
        {
            WLOG_ERROR("hook: enable all failed (%s)", MH_StatusToString(s));
            return false;
        }
        return true;
    }
}
