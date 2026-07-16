// DLL entry: open the log, init hooks, wait for the graphics device, install render detours.
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

#include <windows.h>

#include "core/Hook.hpp"
#include "core/Logger.hpp"
#include "game/Catalog.hpp"
#include "game/gx/Gx.hpp"
#include "runtime/GameHooks.hpp"
#include "runtime/ModuleInstall.hpp"
#include "runtime/InputHooks.hpp"
#include "runtime/LuaBindings.hpp"
#include "runtime/PhasingHooks.hpp"
#include "runtime/RenderHooks.hpp"
#include "runtime/storage/StorageHook.hpp"

/** 
 * @brief IAT anchor; the patcher imports this symbol so the loader maps the DLL.
 */
extern "C" __declspec(dllexport) void WarcraftXL() {}

namespace
{
    constexpr int kDeviceWaitTicks = 600; // ~60 s at 100 ms

    /**
     * @brief Waits for the graphics device, then installs every detour and enables all hooks.
     * @return thread exit code (0).
     */
    DWORD WINAPI MainThread(LPVOID)
    {
        // Time-sensitive hooks must be live before the client builds its message-handler tables.
        wxl::runtime::modules::RunEarly();
        wxl::runtime::storage::Install();

        // Wait for the graphics device (and the window) before installing detours that publish the
        // events the runtime scripts subscribed to at load time. The detours are enabled in one batch
        // after all installers have registered.
        for (int i = 0; i < kDeviceWaitTicks && !wxl::game::gx::RawDevice(); ++i)
            Sleep(100);

        wxl::runtime::render::Install(); // device/render events: OnEndScene, OnFrame, OnM2BatchDraw, ...
        wxl::runtime::modules::RunAll(); // module-registered installers (wxl-modern-adt, ...)
        wxl::runtime::game::Install();   // game events: OnModelLoad, ...
        wxl::runtime::input::Install();  // window events: OnInput
        wxl::runtime::phasing::Install(); // terrain-phase per-tile loader redirect
        wxl::runtime::lua::InstallHooks();
        wxl::core::hook::EnableAll();
        wxl::runtime::lua::Activate();
        wxl::runtime::lua::Install(true);

        WLOG_INFO("wxl-core ready");
        return 0;
    }
}

/**
 * @brief DLL entry point; on process attach opens the log, inits hooks, and spawns the main thread.
 * @param module  DLL module handle.
 * @param reason  reason code for the call.
 * @return TRUE to keep the DLL loaded.
 */
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(module);

        CreateDirectoryA("Logs", nullptr);
        wxl::core::log::Open("Logs\\wxl-core.log");
        WLOG_INFO("wxl-core starting (build %s %s)", __DATE__, __TIME__);

        wxl::core::hook::Init();
        wxl::game::RegisterAllBindings();
        wxl::runtime::game::ReserveM2Memory();

        // Arm the archive-mount guard now, on the loader thread, before the client builds its archive
        // set: the deferred main thread below is raced past by the client's startup, so a guard armed
        // there would miss the mount. Drops the host-owned loose directories so the client stays lean.
        wxl::runtime::storage::InstallArchiveGuard();

        // Module boot installers: memory patches that must land before the client's own startup
        // code runs (boot-sized allocations), e.g. the wxl-modern-blp mip-scratch widening.
        wxl::runtime::modules::RunBoot();

        CloseHandle(CreateThread(nullptr, 0, &MainThread, nullptr, 0, nullptr));
    }
    return TRUE;
}
