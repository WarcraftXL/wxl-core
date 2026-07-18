// UI style: scoped style color/var stacks, the id stack and style color queries for wxl.ui.*.
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

#include "engine/lua/methods/ui/Common.hpp"

/// Scoped styling. push_* / pop_* must be balanced within the frame. Color indices are wxl.ui.COL.* and
/// style-var indices are wxl.ui.STYLEVAR.*.
namespace wxl::lua::methods::ui
{
    /// wxl.ui.push_style_color(idx, r, g, b, a): pushes a style color (floats 0..1). idx is wxl.ui.COL.*.
    inline int L_pushStyleColor(lua_State* L)
    {
        if (!InDraw(L, "push_style_color")) return 0;
        const ImGuiCol idx = static_cast<ImGuiCol>(luaL_checkinteger(L, 1));
        ImGui::PushStyleColor(idx, ReadColor4(L, 2));
        return 0;
    }

    /// wxl.ui.pop_style_color([count]): pops count (default 1) pushed style colors.
    inline int L_popStyleColor(lua_State* L)
    {
        if (!InDraw(L, "pop_style_color")) return 0;
        ImGui::PopStyleColor(OptInt(L, 1, 1));
        return 0;
    }

    /// wxl.ui.push_style_var(idx, v[, v2]): pushes a style var. Pass one number for scalar vars, two for
    /// ImVec2 vars (e.g. WindowPadding). idx is wxl.ui.STYLEVAR.*.
    inline int L_pushStyleVar(lua_State* L)
    {
        if (!InDraw(L, "push_style_var")) return 0;
        const ImGuiStyleVar idx = static_cast<ImGuiStyleVar>(luaL_checkinteger(L, 1));
        const float v = static_cast<float>(luaL_checknumber(L, 2));
        if (lua_isnoneornil(L, 3))
            ImGui::PushStyleVar(idx, v);
        else
            ImGui::PushStyleVar(idx, ImVec2(v, static_cast<float>(luaL_checknumber(L, 3))));
        return 0;
    }

    /// wxl.ui.pop_style_var([count]): pops count (default 1) pushed style vars.
    inline int L_popStyleVar(lua_State* L)
    {
        if (!InDraw(L, "pop_style_var")) return 0;
        ImGui::PopStyleVar(OptInt(L, 1, 1));
        return 0;
    }

    /// wxl.ui.push_id(id): pushes onto the ID stack to disambiguate identical labels. id may be a string or
    /// a number.
    inline int L_pushId(lua_State* L)
    {
        if (!InDraw(L, "push_id")) return 0;
        if (lua_type(L, 1) == LUA_TNUMBER)
            ImGui::PushID(static_cast<int>(lua_tointeger(L, 1)));
        else
            ImGui::PushID(luaL_checkstring(L, 1));
        return 0;
    }

    /// wxl.ui.pop_id(): pops one entry from the ID stack.
    inline int L_popId(lua_State* L)
    {
        if (!InDraw(L, "pop_id")) return 0;
        ImGui::PopID();
        return 0;
    }

    /// wxl.ui.get_color(idx) -> r, g, b, a: the style color at idx as floats 0..1 (wxl.ui.COL.*).
    inline int L_getColor(lua_State* L)
    {
        if (!InDraw(L, "get_color")) return 0;
        const ImVec4& c = ImGui::GetStyleColorVec4(static_cast<ImGuiCol>(luaL_checkinteger(L, 1)));
        lua_pushnumber(L, c.x);
        lua_pushnumber(L, c.y);
        lua_pushnumber(L, c.z);
        lua_pushnumber(L, c.w);
        return 4;
    }

    /// wxl.ui.get_style_color_packed(idx) -> packed: the style color at idx as a packed ImU32 (with the
    /// current global alpha applied), ready for the draw-list API.
    inline int L_getStyleColorPacked(lua_State* L)
    {
        if (!InDraw(L, "get_style_color_packed")) return 0;
        const ImU32 c = ImGui::GetColorU32(static_cast<ImGuiCol>(luaL_checkinteger(L, 1)));
        lua_pushnumber(L, static_cast<lua_Number>(static_cast<unsigned>(c)));
        return 1;
    }

    /// Adds the style fields to the ui subtable on top of the stack.
    inline void RegisterStyle(lua_State* L)
    {
        lua_pushcfunction(L, &L_pushStyleColor);      lua_setfield(L, -2, "push_style_color");
        lua_pushcfunction(L, &L_popStyleColor);       lua_setfield(L, -2, "pop_style_color");
        lua_pushcfunction(L, &L_pushStyleVar);        lua_setfield(L, -2, "push_style_var");
        lua_pushcfunction(L, &L_popStyleVar);         lua_setfield(L, -2, "pop_style_var");
        lua_pushcfunction(L, &L_pushId);              lua_setfield(L, -2, "push_id");
        lua_pushcfunction(L, &L_popId);               lua_setfield(L, -2, "pop_id");
        lua_pushcfunction(L, &L_getColor);            lua_setfield(L, -2, "get_color");
        lua_pushcfunction(L, &L_getStyleColorPacked); lua_setfield(L, -2, "get_style_color_packed");
    }
}
