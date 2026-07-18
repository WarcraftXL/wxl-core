// DLL entry: open the log, init the engine, then unroll the registered features across boot phases.
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

#include "engine/hook/Hook.hpp"
#include "engine/hook/Registry.hpp"
#include "engine/storage/StorageHook.hpp"
#include "common/Log.hpp"
#include "game/Catalog.hpp"
#include "game/gx/Gx.hpp"
#include "runtime/ModuleInstall.hpp"

/**
 * @brief IAT anchor; the patcher imports this symbol so the loader maps the DLL.
 */
extern "C" __declspec(dllexport) void WarcraftXL() {}

namespace
{
    constexpr int kDeviceWaitTicks = 600; // ~60 s at 100 ms

    /**
     * @brief Waits for the graphics device, then installs and enables every registered feature.
     *
     * The engine subsystems that features depend on (hooking, bindings, storage) are already up from
     * DllMain. This thread runs the Normal-phase installers once the device exists (so a feature can
     * safely reference render state), enables the whole detour batch, then runs the PostEnable-phase
     * steps that need the detours already live.
     * @return thread exit code (0).
     */
    DWORD WINAPI MainThread(LPVOID)
    {
        wxl::runtime::modules::RunEarly();
        wxl::runtime::storage::Install();

        // Wait for the graphics device (and the window) before installing detours that publish the
        // events runtime scripts subscribed to at load time.
        for (int i = 0; i < kDeviceWaitTicks && !wxl::game::gx::RawDevice(); ++i)
            Sleep(100);

        wxl::hook::InstallRegisteredFeatures(wxl::hook::Phase::Normal);
        wxl::runtime::modules::RunAll(); // module-registered installers (wxl-modern-adt, ...)
        wxl::hook::EnableAll();
        wxl::hook::InstallRegisteredFeatures(wxl::hook::Phase::PostEnable);

        WLOG_INFO("wxl-core ready");
        return 0;
    }
}

/**
 * @brief DLL entry point; on process attach opens the log, inits the engine, and spawns the main thread.
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
        wxl::log::Open("Logs\\wxl-core.log");
        WLOG_INFO("wxl-core starting (build %s %s)", __DATE__, __TIME__);

        wxl::hook::Init();
        wxl::game::RegisterAllBindings();

        // Arm the archive-mount guard now, on the loader thread, before the client builds its archive
        // set: the deferred main thread below is raced past by the client's startup. Drops the host-owned
        // loose directories so the client stays lean.
        wxl::runtime::storage::InstallArchiveGuard();

        // Boot-phase features run here, on the loader thread, before the client's own startup proceeds:
        // the M2 arena reservation must precede world-load VA fragmentation, and the disk-queue worker
        // extension must be live before the client creates its own disk-queue thread. Enable the batch
        // immediately so these detours are armed when the client boot code reaches them.
        wxl::hook::InstallRegisteredFeatures(wxl::hook::Phase::Boot);
        wxl::hook::EnableAll();

        // Module boot installers: memory patches that must land before the client's own startup code
        // runs (boot-sized allocations), e.g. the wxl-modern-blp mip-scratch widening.
        wxl::runtime::modules::RunBoot();

        CloseHandle(CreateThread(nullptr, 0, &MainThread, nullptr, 0, nullptr));
    }
    return TRUE;
}
