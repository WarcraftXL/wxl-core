// World lifecycle detours: publish world enter/leave and per-frame update events, plus a liquid-row guard.
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

#include "config.hpp"
#include "engine/hook/Hook.hpp"
#include "engine/hook/Registry.hpp"
#include "engine/events/Event.hpp"
#include "features/diag/AssetProfile.hpp"
#include "features/luabindings/LuaBindings.hpp"

#include "common/Log.hpp"
#include "common/Mem.hpp"
#include "offsets/engine/Frame.hpp"
#include "offsets/game/ADT.hpp"
#include "offsets/game/World.hpp"

#include <cstdint>

namespace
{
    namespace ev    = wxl::events;
    namespace wld   = wxl::offsets::game::world;
    namespace frame = wxl::offsets::engine::frame;
    namespace adt   = wxl::offsets::game::adt;
    namespace aprof = wxl::runtime::assetprof;

    wld::World_EnterFn g_origWorldEnter = nullptr;
    frame::FramePumpFn g_origFramePump  = nullptr;

    /**
     * @brief Detours world enter, emitting OnWorldLeave before and OnWorldEnter after the transition.
     *
     * Re-registers the client-VM globals on the fresh FrameScript state the transition creates, so a
     * newly entered world sees WXL's Lua functions/CVars immediately (a new state is detected here).
     * @param worldTime          target world time.
     * @param withLoadingScreen  nonzero to show the loading screen.
     */
    void __cdecl hkWorldEnter(int worldTime, int withLoadingScreen)
    {
        const auto mapId = static_cast<uint32_t>(*reinterpret_cast<int32_t*>(wld::kCurrentMapId));
        ev::WorldLeaveArgs leave{ mapId }; // old world still loaded: id is the one being left
        ev::Emit(ev::Event::OnWorldLeave, &leave);
        g_origWorldEnter(worldTime, withLoadingScreen);
        wxl::runtime::lua::Install(true);
        const auto entered = static_cast<uint32_t>(*reinterpret_cast<int32_t*>(wld::kCurrentMapId));
        ev::WorldEnterArgs enter{ entered };
        ev::Emit(ev::Event::OnWorldEnter, &enter);
    }

    /**
     * @brief Detours the master per-frame pump, emitting OnUpdate once per frame with the frame delta.
     */
    void __cdecl hkFramePump()
    {
        g_origFramePump();
        ev::UpdateArgs a{ *reinterpret_cast<float*>(frame::kDeltaSeconds),
                          *reinterpret_cast<uint32_t*>(frame::kFrameTimeMs) };
        ev::Emit(ev::Event::OnUpdate, &a);
        aprof::RecordFrame(a.dt);
    }

    bool InstallWorld()
    {
        wxl::hook::Install("CWorldEnter", wld::kEnter, &hkWorldEnter, &g_origWorldEnter);
        wxl::hook::Install("FramePump", frame::kFramePump, &hkFramePump, &g_origFramePump);

        // Liquid-row null guard: this one liquid consumer dereferences the LiquidType row flag without the
        // null check the others have, so an unknown liquid id (from any served source) faults. Skip the
        // flag test and make the branch unconditional (nop x4 + jz->jmp) -> default no-bump path.
        const uint8_t guard[5] = { 0x90, 0x90, 0x90, 0x90, 0xEB };
        wxl::mem::Patch(reinterpret_cast<void*>(adt::kLiquidRowFlagTest), guard, sizeof guard);
        return true;
    }
}

WXL_REGISTER_FEATURE("world", wxl::features::kWorld, InstallWorld)
