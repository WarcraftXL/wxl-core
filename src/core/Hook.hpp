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

#pragma once

#include <cstdint>

/// Named hooking over MinHook: a feature installs a hook by name and address; the trampoline is
/// returned through the original out-pointer.
namespace wxl::core::hook
{
    /**
     * @brief Initialises the hooking engine once at startup.
     * @return true if initialisation succeeded.
     */
    bool Init();

    /**
     * @brief Installs one detour.
     * @param name      label used for logging.
     * @param target    engine function address to detour.
     * @param detour    replacement function.
     * @param original  receives the trampoline to the original function.
     * @return true if the detour was created.
     */
    bool Install(const char* name, void* target, void* detour, void** original);

    /**
     * @brief Installs one detour, taking the target as an integer address.
     * @param name      label used for logging.
     * @param target    engine function address to detour.
     * @param detour    replacement function.
     * @param original  receives the trampoline to the original function.
     * @return true if the detour was created.
     */
    inline bool Install(const char* name, uintptr_t target, void* detour, void** original)
    {
        return Install(name, reinterpret_cast<void*>(target), detour, original);
    }

    /**
     * @brief Enables every installed hook, called after all features have registered.
     * @return true if all hooks were enabled.
     */
    bool EnableAll();
}
