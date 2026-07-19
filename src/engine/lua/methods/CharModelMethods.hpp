// CharModel method context: the wxl.charmodel subtable reading a character-model event pointer's ids.
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
#include "engine/lua/methods/PointerArg.hpp"
#include "offsets/game/M2.hpp"

/// The CharModel context, in the CameraMethods mould: Register() adds the wxl.charmodel subtable to the
/// `wxl` table on the stack. Each field takes the CharModelObject pointer an "item_slot_change" /
/// "item_slot_clear" handler received as LIGHTUSERDATA and reads its identity ids via an SEH-guarded POD
/// read off a confirmed offset, so a stale or wrong-typed event pointer yields nil rather than faulting.
/// Header-only and inline.
namespace wxl::lua::methods::charmodel
{
    namespace off = wxl::offsets::game::m2;

    /// wxl.charmodel.race(char_model) -> int or nil (race id). char_model is an item_slot_change /
    /// item_slot_clear event lightuserdata.
    inline int L_race(lua_State* L)
    {
        void* cmo = CheckPtr(L, 1);
        uint32_t v = 0;
        if (!cmo || !SehReadU32(cmo, off::kOffCmoRace, v))
        {
            PushNil(L);
            return 1;
        }
        Push(L, static_cast<lua_Integer>(v));
        return 1;
    }

    /// wxl.charmodel.gender(char_model) -> int or nil (0 = male, 1 = female). char_model is an
    /// item_slot_change / item_slot_clear event lightuserdata.
    inline int L_gender(lua_State* L)
    {
        void* cmo = CheckPtr(L, 1);
        uint32_t v = 0;
        if (!cmo || !SehReadU32(cmo, off::kOffCmoGender, v))
        {
            PushNil(L);
            return 1;
        }
        Push(L, static_cast<lua_Integer>(v));
        return 1;
    }

    /**
     * @brief Adds the wxl.charmodel subtable to the table on top of the stack. Stack-neutral.
     * @param L  the engine lua_State, with the target `wxl` table at index -1.
     */
    inline void Register(lua_State* L)
    {
        static const luaL_Reg fns[] = {
            { "race",   L_race },
            { "gender", L_gender },
            { nullptr, nullptr },
        };
        RegisterModule(L, "charmodel", fns);
    }
}
