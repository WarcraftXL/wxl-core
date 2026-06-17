// DLL entry: open the log, install storage hooks, start the host, bootstrap WMO guards.
// Copyright (C) 2026 WraithEngine
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

#include "Logger.hpp"
#include "Hook.hpp"
#include "WmoGuards.hpp"
#include "StorageHook.hpp"
#include "ShmClient.hpp"
#include "runtime/m2/Loader.hpp"
#include "runtime/m2/Anim.hpp"
#include "runtime/m2/Ribbon.hpp"
#include "runtime/m2/Particle.hpp"
#include "runtime/m2/Bone.hpp"
#include "runtime/m2/Mahjong.hpp"
#include "runtime/adt/Adt.hpp"
#include "runtime/db2/DB2Mgr.hpp"
#include "runtime/db2/MapOverride.hpp"

extern "C" __declspec(dllexport) void Wraith() {}

namespace
{
    // Remaining engine feature hooks, installed off the loader lock.
    DWORD WINAPI Bootstrap(LPVOID)
    {
        wraith::runtime::wmo::Install();

        // M2 runtime fixups on the host-produced target-shaped model. Loader widens the version gate and
        // rebuilds the live skin material contract; the others are live-object/GPU corrections.
        wraith::runtime::m2::Install();
        wraith::runtime::anim::Install();
        wraith::runtime::ribbon::Install();
        wraith::runtime::particle::Install();
        wraith::runtime::bone::Install();
        wraith::runtime::mahjong::Install();
        wraith::runtime::adt::Install();

        // Data tables: feed the engine modern table data and serve the modern Map.db2 over the stock
        // map storage so modern maps load.
        wraith::features::db2::Install();
        wraith::features::db2::InstallMapOverride();

        wraith::hook::EnableAll();
        WLOG_INFO("WRAITH features ready.");
        return 0;
    }

    // Max wait for the host at launch; on timeout the client boots on its native archives.
    constexpr uint32_t kHostReadyTimeoutMs = 15000;
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(module);

        CreateDirectoryA("Logs", nullptr);
        wraith::core::log::Open("Logs\\Wraith.log");
        WLOG_INFO("WRAITH starting (build %s %s)", __DATE__, __TIME__);

        if (wraith::hook::Init())
        {
            // Storm hooks must be live before the engine opens its first file.
            wraith::runtime::storage::Install();
            wraith::hook::EnableAll();

            // Hold the loader here until the host is ready, so every file open is served by the host from the
            // start and the client never reads its own archives (mounted archives remain only as a fallback).
            wraith::runtime::ipc::EnsureHostRunning();
            wraith::runtime::ipc::WaitForHost(kHostReadyTimeoutMs);

            CloseHandle(CreateThread(nullptr, 0, &Bootstrap, nullptr, 0, nullptr));
        }
    }
    return TRUE;
}
