// Map-object load detours: publish OnWmoRootLoad and OnWmoGroupLoad while the buffers are still raw.
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

#include "engine/events/Event.hpp"
#include "engine/hook/Hook.hpp"
#include "engine/hook/Registry.hpp"
#include "runtime/HookPoints.hpp"

#include "wxl/offsets/game/WMO.hpp"

namespace
{
    namespace ev  = wxl::events;
    namespace wmo = wxl::offsets::game::wmo;

    wmo::Wmo_RootCompleteFn g_origRootComplete = nullptr;
    wmo::WmoGroup_ParseFn   g_origGroupParse   = nullptr;

    /**
     * @brief Detours root read-completion, emitting OnWmoRootLoad before the chunk walker runs.
     *
     * Emitted on the way in, not out: the root buffer is still the bytes the read produced, which is
     * the window a subscriber reshaping it through wxl::game::wmo needs. kRootComplete is the single
     * caller of the root finaliser, so every root parse passes through here.
     * @param root  root buffer the async read just filled.
     */
    void __cdecl hkRootComplete(void* root)
    {
        ev::WmoRootLoadArgs a{ root };
        ev::Emit<ev::Event::OnWmoRootLoad>(a);
        g_origRootComplete(root);
    }

    /**
     * @brief Detours the group reader, emitting OnWmoGroupLoad before the sub-chunk walk.
     *
     * Hooked at the reader rather than at the group read-completion callback: the reader is the join
     * point of the sync and async group-load paths, so a group loaded synchronously is published too.
     * @param group  group buffer, still holding the bytes the read produced.
     * @param edx    unused; the register the native convention passes nothing meaningful in.
     */
    void __fastcall hkGroupParse(void* group, void* edx)
    {
        ev::WmoGroupLoadArgs a{ group };
        ev::Emit<ev::Event::OnWmoGroupLoad>(a);
        g_origGroupParse(group, edx);
    }

    bool InstallWmoLoad()
    {
        wxl::runtime::hookpoints::Attach("Wmo.RootComplete", &hkRootComplete, &g_origRootComplete);
        wxl::runtime::hookpoints::Attach("Wmo.GroupParse", &hkGroupParse, &g_origGroupParse);
        return true;
    }
}

WXL_REGISTER_FEATURE("wmo-load", true, InstallWmoLoad)
