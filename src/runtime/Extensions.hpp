// Discovery and loading of out-of-core extensions.
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

namespace wxl::runtime::extensions
{
    /**
     * @brief Arms the detour that loads the extensions.
     *
     * Called from DllMain, where LoadLibrary is not an option: the loading itself happens later,
     * from the engine-initialisation seam, on the main thread and outside the loader lock. That
     * point still precedes the reader-queue setup and the texture scratch sizing, so an extension
     * can reach either.
     *
     * Extensions load one folder per extension out of Extensions/, in name order, each holding a DLL
     * of the same name -- the layout the client already uses for its own addons. Each is queried
     * before any of its code runs; a refusal or a failure never stops the others.
     * @return true if the detour was registered.
     */
    bool InstallLoader();

    /**
     * @brief Loads the extensions now, on the first call; every later call returns at once.
     *
     * Two seams call it, whichever the client reaches first. GxDevCreate is the earlier one: the
     * client's startup builds the graphics device (ConsoleDeviceInitialize, 0x0076AB80, called
     * from 0x00406B28) before it even schedules the engine-initialisation callback (0x004047E0,
     * pushed at 0x00406B4C), so a device factory, a caps detour or a shader detour attached from
     * the engine-init seam would arrive after the device it was meant for. The engine-init seam
     * stays the loader when the device-create feature is not installed.
     *
     * Main thread, outside the loader lock, in both cases.
     */
    void EnsureLoaded();

    /**
     * @brief Publishes a service into the same interface table WXL_Api::PublishInterface writes.
     *
     * For a core-owned resource an extension must reach without a direct link -- today just the
     * large-M2 arena (wxl.m2arena), whose boot-phase VA reservation has to run before any extension
     * exists to receive it. Safe to call from a Boot-phase feature: the table itself needs no
     * extension to be loaded yet, only GetInterface does.
     * @param name     agreed service name.
     * @param version  agreed service version.
     * @param iface    pointer to the service, valid for the process lifetime.
     */
    void PublishInterface(const char* name, uint32_t version, void* iface);
}
