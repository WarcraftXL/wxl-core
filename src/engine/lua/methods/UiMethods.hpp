// UI method context: the wxl.ui.* immediate-mode surface (windows + MVP widgets) over Dear ImGui.
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
#include "engine/lua/ui/ImGuiHost.hpp"

#include "imgui.h"

/// The wxl.ui.* surface: immediate-mode widgets a Lua extension calls to describe a window, re-run every
/// frame. Same shape as any method context -- Register() adds the fields to the `wxl` table on the stack,
/// here under a `ui` subtable. Header-only inline, one context, no separate TU.
///
/// CONTRACT: every wxl.ui.* call is valid ONLY inside a handler of the 'draw' event (wxl.on("draw", ...)),
/// which the ImGui host fires once per frame with the frame open. A call from anywhere else raises a Lua
/// error rather than corrupting ImGui state. `end` is a Lua keyword, so the window closer is wxl.ui.finish.
namespace wxl::lua::methods::ui
{
    /// Rejects a call made outside the open ImGui frame. On failure it longjmps out via luaL_error and
    /// never returns; callers use it as `if (!InDraw(L, name)) return 0;` purely for readability.
    inline bool InDraw(lua_State* L, const char* fn)
    {
        if (wxl::lua::ui::InFrame())
            return true;
        luaL_error(L, "wxl.ui.%s: only valid inside a 'draw' handler (wxl.on(\"draw\", ...))", fn);
        return false; // unreachable: luaL_error does not return
    }

    /// wxl.ui.begin(title) -> visible: opens a window. visible is false when the window is collapsed or
    /// clipped; a script may skip building its body then. wxl.ui.finish MUST still be called regardless.
    inline int L_begin(lua_State* L)
    {
        if (!InDraw(L, "begin")) return 0;
        const char* title = luaL_checkstring(L, 1);
        lua_pushboolean(L, ImGui::Begin(title));
        return 1;
    }

    /// wxl.ui.finish(): closes the window opened by the matching wxl.ui.begin. Always call it, even when
    /// begin returned false (the ImGui begin/end pairing is unconditional).
    inline int L_finish(lua_State* L)
    {
        if (!InDraw(L, "finish")) return 0;
        ImGui::End();
        return 0;
    }

    /// wxl.ui.text(s): draws a line of text. Rendered unformatted, so a % in the string is literal.
    inline int L_text(lua_State* L)
    {
        if (!InDraw(L, "text")) return 0;
        ImGui::TextUnformatted(luaL_checkstring(L, 1));
        return 0;
    }

    /// wxl.ui.button(label) -> clicked: draws a button, true on the frame it is pressed.
    inline int L_button(lua_State* L)
    {
        if (!InDraw(L, "button")) return 0;
        lua_pushboolean(L, ImGui::Button(luaL_checkstring(L, 1)));
        return 1;
    }

    /// wxl.ui.checkbox(label, state) -> newState: draws a checkbox seeded from state, returns its value
    /// after this frame's interaction (immediate mode: the script feeds the value back next frame).
    inline int L_checkbox(lua_State* L)
    {
        if (!InDraw(L, "checkbox")) return 0;
        const char* label = luaL_checkstring(L, 1);
        bool        state = lua_toboolean(L, 2) != 0;
        ImGui::Checkbox(label, &state);
        lua_pushboolean(L, state);
        return 1;
    }

    /// wxl.ui.slider(label, v, min, max) -> newV: draws a float slider seeded from v, returns its value
    /// after this frame's drag.
    inline int L_slider(lua_State* L)
    {
        if (!InDraw(L, "slider")) return 0;
        const char* label = luaL_checkstring(L, 1);
        float       v     = static_cast<float>(luaL_checknumber(L, 2));
        const float mn    = static_cast<float>(luaL_checknumber(L, 3));
        const float mx    = static_cast<float>(luaL_checknumber(L, 4));
        ImGui::SliderFloat(label, &v, mn, mx);
        lua_pushnumber(L, v);
        return 1;
    }

    /// wxl.ui.separator(): draws a horizontal separator line.
    inline int L_separator(lua_State* L)
    {
        if (!InDraw(L, "separator")) return 0;
        ImGui::Separator();
        return 0;
    }

    /// wxl.ui.same_line(): keeps the next widget on the current line instead of a new one.
    inline int L_sameLine(lua_State* L)
    {
        if (!InDraw(L, "same_line")) return 0;
        ImGui::SameLine();
        return 0;
    }

    /**
     * @brief Adds the `ui` subtable (wxl.ui.*) to the table on top of the stack. Stack-neutral.
     * @param L  the engine lua_State, with the target `wxl` table at index -1.
     */
    inline void Register(lua_State* L)
    {
        lua_newtable(L); // the wxl.ui subtable
        lua_pushcfunction(L, &L_begin);     lua_setfield(L, -2, "begin");
        lua_pushcfunction(L, &L_finish);    lua_setfield(L, -2, "finish");
        lua_pushcfunction(L, &L_text);      lua_setfield(L, -2, "text");
        lua_pushcfunction(L, &L_button);    lua_setfield(L, -2, "button");
        lua_pushcfunction(L, &L_checkbox);  lua_setfield(L, -2, "checkbox");
        lua_pushcfunction(L, &L_slider);    lua_setfield(L, -2, "slider");
        lua_pushcfunction(L, &L_separator); lua_setfield(L, -2, "separator");
        lua_pushcfunction(L, &L_sameLine);  lua_setfield(L, -2, "same_line");
        lua_setfield(L, -2, "ui"); // wxl.ui = subtable
    }
}
