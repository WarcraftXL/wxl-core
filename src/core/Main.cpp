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
#include "runtime/InputHooks.hpp"
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
        wxl::runtime::storage::Install();

        // Wait for the graphics device (and the window) before installing detours that publish the
        // events the runtime scripts subscribed to at load time. The detours are enabled in one batch
        // after all installers have registered.
        for (int i = 0; i < kDeviceWaitTicks && !wxl::game::gx::RawDevice(); ++i)
            Sleep(100);

        wxl::runtime::render::Install(); // device/render events: OnEndScene, OnFrame, OnM2BatchDraw, ...
        wxl::runtime::game::Install();   // game events: OnModelLoad, ...
        wxl::runtime::input::Install();  // window events: OnInput
        wxl::core::hook::EnableAll();

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
        CloseHandle(CreateThread(nullptr, 0, &MainThread, nullptr, 0, nullptr));
    }
    return TRUE;
}
