// Unit/object detours: publish object update/destroy and target-change events from the message handlers.
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

#include "common/Log.hpp"
#include "offsets/game/Unit.hpp"

namespace
{
    namespace ev   = wxl::events;
    namespace unit = wxl::offsets::game::unit;

    unit::ObjectMsgHandlerFn g_origObjUpdate  = nullptr;
    unit::ObjectMsgHandlerFn g_origObjDestroy = nullptr;
    unit::TargetSetFn        g_origTargetSet  = nullptr;

    /**
     * @brief Detours the server object update-block handler, emitting OnObjectUpdate after the parse.
     *
     * One fire per update message (a batch of created/updated objects). Logs the first fire only.
     * @param ctx     handler context.
     * @param opcode  message opcode.
     * @param msg     message id.
     * @param packet  inbound message reader.
     * @return the native handler result.
     */
    int __cdecl hkObjUpdate(void* ctx, int opcode, int msg, void* packet)
    {
        const int r = g_origObjUpdate(ctx, opcode, msg, packet);

        ev::ObjectUpdateArgs a{ packet, opcode };
        ev::Emit(ev::Event::OnObjectUpdate, &a);

        static bool logged = false;
        if (!logged) { logged = true; WLOG_INFO("object: update stream active"); }
        return r;
    }

    /**
     * @brief Detours the object destroy handler, emitting OnObjectDestroy before the despawn.
     *
     * One fire per despawn, while the object is still resident. Logs the first fire only.
     * @param ctx     handler context.
     * @param opcode  message opcode.
     * @param msg     message id.
     * @param packet  inbound message reader (object GUID + on-death flag).
     * @return the native handler result.
     */
    int __cdecl hkObjDestroy(void* ctx, int opcode, int msg, void* packet)
    {
        ev::ObjectDestroyArgs a{ packet, opcode };
        ev::Emit(ev::Event::OnObjectDestroy, &a);

        static bool logged = false;
        if (!logged) { logged = true; WLOG_INFO("object: destroy hook active"); }
        return g_origObjDestroy(ctx, opcode, msg, packet);
    }

    /**
     * @brief Detours the target-set API, emitting OnTargetChanged after the new target is applied.
     * @param scriptState  script state the call ran on.
     * @return the native function result.
     */
    int __cdecl hkTargetSet(void* scriptState)
    {
        const int r = g_origTargetSet(scriptState);

        ev::TargetChangedArgs a{ scriptState };
        ev::Emit(ev::Event::OnTargetChanged, &a);

        // Log the first fire only: target changes are a per-combat-action event.
        static bool logged = false;
        if (!logged) { logged = true; WLOG_INFO("target: hook live (first change)"); }
        return r;
    }

    bool InstallUnit()
    {
        wxl::hook::Install("ObjectUpdate", unit::kObjectUpdateHandler, &hkObjUpdate, &g_origObjUpdate);
        wxl::hook::Install("ObjectDestroy", unit::kObjectDestroyHandler, &hkObjDestroy, &g_origObjDestroy);
        wxl::hook::Install("TargetSet", unit::kTargetSet, &hkTargetSet, &g_origTargetSet);
        return true;
    }
}

WXL_REGISTER_FEATURE("unit", wxl::features::kUnit, InstallUnit)
