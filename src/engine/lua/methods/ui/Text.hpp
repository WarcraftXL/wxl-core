// UI text: plain, colored, disabled, wrapped, labelled and bulleted text output for wxl.ui.*.
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

/// Text output. All strings are passed through "%s" so a literal % never triggers printf formatting.
namespace wxl::lua::methods::ui
{
    /// wxl.ui.text(s): draws a line of text. Rendered unformatted, so a % in the string is literal.
    inline int L_text(lua_State* L)
    {
        if (!InDraw(L, "text")) return 0;
        ImGui::TextUnformatted(luaL_checkstring(L, 1));
        return 0;
    }

    /// wxl.ui.text_colored(r, g, b, a, s): text tinted with the given color (floats 0..1).
    inline int L_textColored(lua_State* L)
    {
        if (!InDraw(L, "text_colored")) return 0;
        const ImVec4 col = ReadColor4(L, 1);
        ImGui::TextColored(col, "%s", luaL_checkstring(L, 5));
        return 0;
    }

    /// wxl.ui.text_disabled(s): text in the greyed-out disabled color.
    inline int L_textDisabled(lua_State* L)
    {
        if (!InDraw(L, "text_disabled")) return 0;
        ImGui::TextDisabled("%s", luaL_checkstring(L, 1));
        return 0;
    }

    /// wxl.ui.text_wrapped(s): text that wraps at the window's right edge.
    inline int L_textWrapped(lua_State* L)
    {
        if (!InDraw(L, "text_wrapped")) return 0;
        ImGui::TextWrapped("%s", luaL_checkstring(L, 1));
        return 0;
    }

    /// wxl.ui.label_text(label, s): a value string aligned the same way as value+label widgets.
    inline int L_labelText(lua_State* L)
    {
        if (!InDraw(L, "label_text")) return 0;
        const char* label = luaL_checkstring(L, 1);
        ImGui::LabelText(label, "%s", luaL_checkstring(L, 2));
        return 0;
    }

    /// wxl.ui.bullet_text(s): text preceded by a bullet.
    inline int L_bulletText(lua_State* L)
    {
        if (!InDraw(L, "bullet_text")) return 0;
        ImGui::BulletText("%s", luaL_checkstring(L, 1));
        return 0;
    }

    /// wxl.ui.bullet(): draws a bullet and keeps the cursor on the same line for a following item.
    inline int L_bullet(lua_State* L)
    {
        if (!InDraw(L, "bullet")) return 0;
        ImGui::Bullet();
        return 0;
    }

    /// Adds the text fields to the ui subtable on top of the stack.
    inline void RegisterText(lua_State* L)
    {
        lua_pushcfunction(L, &L_text);         lua_setfield(L, -2, "text");
        lua_pushcfunction(L, &L_textColored);  lua_setfield(L, -2, "text_colored");
        lua_pushcfunction(L, &L_textDisabled); lua_setfield(L, -2, "text_disabled");
        lua_pushcfunction(L, &L_textWrapped);  lua_setfield(L, -2, "text_wrapped");
        lua_pushcfunction(L, &L_labelText);    lua_setfield(L, -2, "label_text");
        lua_pushcfunction(L, &L_bulletText);   lua_setfield(L, -2, "bullet_text");
        lua_pushcfunction(L, &L_bullet);       lua_setfield(L, -2, "bullet");
    }
}
