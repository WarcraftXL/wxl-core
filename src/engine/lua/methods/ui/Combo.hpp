// UI combo & selection: combo boxes, selectables and list boxes for wxl.ui.*.
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

#include <string>
#include <vector>

#include "engine/lua/Marshal.hpp"
#include "engine/lua/methods/ui/Common.hpp"

/// Combo boxes and list selection. The convenience combo()/list_box() take a Lua array of item strings and
/// speak 1-based Lua indices, converting to/from ImGui's 0-based item index internally.
namespace wxl::lua::methods::ui
{
    /// Copies the string array at stack index i into out (kept alive for the call), returning the count.
    inline int ReadItemArray(lua_State* L, int i, std::vector<std::string>& out)
    {
        luaL_checktype(L, i, LUA_TTABLE);
        const int n = static_cast<int>(lua_objlen(L, i));
        out.reserve(static_cast<size_t>(n));
        for (int k = 1; k <= n; ++k)
        {
            lua_rawgeti(L, i, k);
            out.emplace_back(lua_isstring(L, -1) ? lua_tostring(L, -1) : "");
            lua_pop(L, 1);
        }
        return n;
    }

    /// wxl.ui.begin_combo(label, preview[, flags]) -> open: opens a combo box showing preview. When it
    /// returns true, emit selectable() items and call end_combo.
    inline int L_beginCombo(lua_State* L)
    {
        if (!InDraw(L, "begin_combo")) return 0;
        const char* label   = luaL_checkstring(L, 1);
        const char* preview = luaL_checkstring(L, 2);
        lua_pushboolean(L, ImGui::BeginCombo(label, preview, OptFlags(L, 3)));
        return 1;
    }

    /// wxl.ui.end_combo(): closes a combo opened by begin_combo (call only when it returned true).
    inline int L_endCombo(lua_State* L)
    {
        if (!InDraw(L, "end_combo")) return 0;
        ImGui::EndCombo();
        return 0;
    }

    /// wxl.ui.selectable(label[, selected[, flags]]) -> clicked: a selectable row drawn highlighted when
    /// selected is true; clicked is true on the frame it is pressed.
    inline int L_selectable(lua_State* L)
    {
        if (!InDraw(L, "selectable")) return 0;
        const char* label    = luaL_checkstring(L, 1);
        const bool  selected = OptBool(L, 2, false);
        lua_pushboolean(L, ImGui::Selectable(label, selected, OptFlags(L, 3)));
        return 1;
    }

    /// wxl.ui.combo(label, index, items) -> index, changed: a one-call combo over a Lua array of strings.
    /// index is 1-based; changed is true on the frame the selection changes.
    inline int L_combo(lua_State* L)
    {
        if (!InDraw(L, "combo")) return 0;
        const char* label = luaL_checkstring(L, 1);
        int         idx   = static_cast<int>(luaL_checkinteger(L, 2)) - 1; // 1-based Lua -> 0-based ImGui
        std::vector<std::string> items;
        const int   n     = ReadItemArray(L, 3, items);
        std::vector<const char*> ptrs;
        ptrs.reserve(items.size());
        for (const auto& s : items) ptrs.push_back(s.c_str());
        const bool changed = ImGui::Combo(label, &idx, ptrs.empty() ? nullptr : ptrs.data(), n);
        lua_pushinteger(L, idx + 1); // back to 1-based
        lua_pushboolean(L, changed);
        return 2;
    }

    /// wxl.ui.list_box(label, index, items) -> index, changed: a framed scrolling list over a Lua array of
    /// strings. index is 1-based; changed is true on the frame the selection changes.
    inline int L_listBox(lua_State* L)
    {
        if (!InDraw(L, "list_box")) return 0;
        const char* label = luaL_checkstring(L, 1);
        int         idx   = static_cast<int>(luaL_checkinteger(L, 2)) - 1; // 1-based Lua -> 0-based ImGui
        std::vector<std::string> items;
        const int   n     = ReadItemArray(L, 3, items);
        std::vector<const char*> ptrs;
        ptrs.reserve(items.size());
        for (const auto& s : items) ptrs.push_back(s.c_str());
        const bool changed = ImGui::ListBox(label, &idx, ptrs.empty() ? nullptr : ptrs.data(), n);
        lua_pushinteger(L, idx + 1); // back to 1-based
        lua_pushboolean(L, changed);
        return 2;
    }

    /// Adds the combo/selection fields to the ui subtable on top of the stack.
    inline void RegisterCombo(lua_State* L)
    {
        static const luaL_Reg fns[] = {
            { "begin_combo", L_beginCombo },
            { "end_combo",   L_endCombo },
            { "selectable",  L_selectable },
            { "combo",       L_combo },
            { "list_box",    L_listBox },
            { nullptr, nullptr },
        };
        SetFunctions(L, fns);
    }
}
