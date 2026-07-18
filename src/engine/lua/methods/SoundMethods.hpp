// Sound method context: the wxl.sound subtable (availability + master volume read/set).
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

#include "engine/lua/LuaJit.hpp"
#include "engine/lua/Marshal.hpp"
#include "game/sound/Sound.hpp"

#include <windows.h>

/// The sound context, in the CameraMethods mould: Register() adds the wxl.sound subtable to the `wxl`
/// table on the stack. Availability and master volume go through the existing wxl::game::sound accessors
/// (a global flag read and a group-record float read); set_master_volume writes through the proven
/// native master-volume setter (the same call modules use to mute), clamped to 0..1. Every call is
/// SEH-guarded so a sound system that is down or mid-teardown yields nil / a no-op rather than a fault.
/// Sound PLAYBACK is deliberately NOT exposed: kPlaySound/kPlaySoundKit take a FrameScript scriptState or
/// an unproven multi-argument shape with no safe game/ wrapper. Header-only and inline.
namespace wxl::lua::methods::sound
{
    namespace gs = wxl::game::sound;

    /// wxl.sound.available() -> bool: true once the client sound system is initialized.
    inline int L_available(lua_State* L)
    {
        bool ok = false;
        __try
        {
            ok = gs::Available();
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            ok = false;
        }
        Push(L, ok);
        return 1;
    }

    /// wxl.sound.master_volume() -> number or nil: the live master volume in 0..1 (1.0 when sound is up
    /// but no group record yet); nil on fault.
    inline int L_masterVolume(lua_State* L)
    {
        float v = 1.0f;
        __try
        {
            v = gs::MasterVolume();
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            PushNil(L);
            return 1;
        }
        Push(L, static_cast<double>(v));
        return 1;
    }

    /// wxl.sound.set_master_volume(v): sets the client master volume, clamped to 0..1. No-op while the
    /// sound system is not up. Goes through the proven native setter modules use to mute.
    inline int L_setMasterVolume(lua_State* L)
    {
        double v = CheckNumber(L, 1);
        if (v < 0.0) v = 0.0;
        if (v > 1.0) v = 1.0;
        __try
        {
            gs::SetMasterVolume(static_cast<float>(v));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            // swallow: a torn-down sound system must not fault the caller.
        }
        return 0;
    }

    /**
     * @brief Adds the wxl.sound subtable to the table on top of the stack. Stack-neutral.
     * @param L  the engine lua_State, with the target `wxl` table at index -1.
     */
    inline void Register(lua_State* L)
    {
        lua_newtable(L);                                                                  // [wxl, sound]
        lua_pushcfunction(L, &L_available);       lua_setfield(L, -2, "available");
        lua_pushcfunction(L, &L_masterVolume);    lua_setfield(L, -2, "master_volume");
        lua_pushcfunction(L, &L_setMasterVolume); lua_setfield(L, -2, "set_master_volume");
        lua_setfield(L, -2, "sound");                                                     // [wxl]
    }
}
