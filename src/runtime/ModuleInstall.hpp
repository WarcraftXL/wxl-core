// Module installer registry: runtime scripts self-register detour installers run from the main thread.
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

namespace wxl::runtime::modules
{
    /**
     * @brief Callback type for a module's detour installer.
     */
    using InstallFn = void(*)();

    /**
     * @brief Registers a module installer.
     *
     * Safe to call from a global constructor (just a function pointer stored in a list). The
     * installers run in registration order on the main thread, after the graphics device exists
     * and before the hook batch is enabled.
     * @param name  short module name, for the install log line
     * @param fn    installer to run
     */
    void Register(const char* name, InstallFn fn);

    /**
     * @brief Runs every registered installer, in registration order.
     *
     * Called once from the main thread, after the device wait and before EnableAll.
     */
    void RunAll();

    /**
     * @brief Registers an installer that must run before the graphics-device wait.
     */
    void RegisterEarly(const char* name, InstallFn fn);

    /** Runs every registered early installer in registration order. */
    void RunEarly();

    /**
     * @brief Registers a boot-time installer.
     *
     * Boot installers run on the loader thread inside DllMain, after the log opens and before the
     * client's own startup code executes. For patches that must precede client init (boot-sized
     * allocations, archive mounts); keep them to memory patches, no hooking or waiting.
     * @param name  short module name, for the install log line
     * @param fn    installer to run
     */
    void RegisterBoot(const char* name, InstallFn fn);

    /**
     * @brief Runs every registered boot installer, in registration order.
     *
     * Called once from DllMain on the loader thread.
     */
    void RunBoot();
}
