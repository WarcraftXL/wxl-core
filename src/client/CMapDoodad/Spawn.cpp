// Doodad-placement spawn detour: publish OnDoodadSpawn with the placement the native call built.
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

#include "engine/hook/Hook.hpp"
#include "runtime/HookPoints.hpp"
#include "engine/hook/Registry.hpp"
#include "engine/events/Event.hpp"

#include "offsets/game/Doodad.hpp"

namespace
{
    namespace ev = wxl::events;
    namespace dd = wxl::offsets::game::doodad;

    dd::SpawnFromMDDFFn g_origDoodadSpawn = nullptr;

    /**
     * @brief Detours doodad spawn, emitting OnDoodadSpawn with the placement the native call built.
     * @param modelName   doodad model name.
     * @param mddf        placement record.
     * @param tileOrigin  tile origin for the placement.
     * @return the spawned doodad.
     */
    void* __cdecl hkDoodadSpawn(const char* modelName, void* mddf, void* tileOrigin)
    {
        void* doodad = g_origDoodadSpawn(modelName, mddf, tileOrigin);
        ev::DoodadSpawnArgs a{ doodad };
        ev::Emit<ev::Event::OnDoodadSpawn>(a);
        return doodad;
    }

    bool InstallDoodadSpawn()
    {
        wxl::runtime::hookpoints::Attach("Doodad.SpawnFromMddf", &hkDoodadSpawn, &g_origDoodadSpawn);
        return true;
    }
}

WXL_REGISTER_FEATURE("doodad-spawn", true, InstallDoodadSpawn)
