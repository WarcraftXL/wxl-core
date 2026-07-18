// UI layout: separators, spacing, indentation, groups and cursor positioning for wxl.ui.*.
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

/// Layout primitives that place, space and align items within the current window.
namespace wxl::lua::methods::ui
{
    /// wxl.ui.separator(): draws a horizontal separator line.
    inline int L_separator(lua_State* L)
    {
        if (!InDraw(L, "separator")) return 0;
        ImGui::Separator();
        return 0;
    }

    /// wxl.ui.separator_text(label): a horizontal separator with a centered text label.
    inline int L_separatorText(lua_State* L)
    {
        if (!InDraw(L, "separator_text")) return 0;
        ImGui::SeparatorText(luaL_checkstring(L, 1));
        return 0;
    }

    /// wxl.ui.same_line([offset[, spacing]]): keeps the next widget on the current line. offset is the
    /// window-local x (0: continue where we are); spacing overrides item spacing (-1: use style default).
    inline int L_sameLine(lua_State* L)
    {
        if (!InDraw(L, "same_line")) return 0;
        ImGui::SameLine(OptFloat(L, 1, 0.0f), OptFloat(L, 2, -1.0f));
        return 0;
    }

    /// wxl.ui.new_line(): undoes a same_line / forces a new line.
    inline int L_newLine(lua_State* L)
    {
        if (!InDraw(L, "new_line")) return 0;
        ImGui::NewLine();
        return 0;
    }

    /// wxl.ui.spacing(): adds a blank vertical gap.
    inline int L_spacing(lua_State* L)
    {
        if (!InDraw(L, "spacing")) return 0;
        ImGui::Spacing();
        return 0;
    }

    /// wxl.ui.dummy(w, h): reserves an empty box of the given size (does not take mouse input).
    inline int L_dummy(lua_State* L)
    {
        if (!InDraw(L, "dummy")) return 0;
        const float w = static_cast<float>(luaL_checknumber(L, 1));
        const float h = static_cast<float>(luaL_checknumber(L, 2));
        ImGui::Dummy(ImVec2(w, h));
        return 0;
    }

    /// wxl.ui.indent([w]): shifts following content right by w (or the style indent when omitted).
    inline int L_indent(lua_State* L)
    {
        if (!InDraw(L, "indent")) return 0;
        ImGui::Indent(OptFloat(L, 1, 0.0f));
        return 0;
    }

    /// wxl.ui.unindent([w]): shifts following content left by w (or the style indent when omitted).
    inline int L_unindent(lua_State* L)
    {
        if (!InDraw(L, "unindent")) return 0;
        ImGui::Unindent(OptFloat(L, 1, 0.0f));
        return 0;
    }

    /// wxl.ui.begin_group(): locks a horizontal start position so the enclosed items act as one item.
    inline int L_beginGroup(lua_State* L)
    {
        if (!InDraw(L, "begin_group")) return 0;
        ImGui::BeginGroup();
        return 0;
    }

    /// wxl.ui.end_group(): closes the group opened by begin_group.
    inline int L_endGroup(lua_State* L)
    {
        if (!InDraw(L, "end_group")) return 0;
        ImGui::EndGroup();
        return 0;
    }

    /// wxl.ui.get_cursor_pos() -> x, y: window-local cursor position (where the next item lands).
    inline int L_getCursorPos(lua_State* L)
    {
        if (!InDraw(L, "get_cursor_pos")) return 0;
        return PushVec2(L, ImGui::GetCursorPos());
    }

    /// wxl.ui.set_cursor_pos(x, y): sets the window-local cursor position.
    inline int L_setCursorPos(lua_State* L)
    {
        if (!InDraw(L, "set_cursor_pos")) return 0;
        const float x = static_cast<float>(luaL_checknumber(L, 1));
        const float y = static_cast<float>(luaL_checknumber(L, 2));
        ImGui::SetCursorPos(ImVec2(x, y));
        return 0;
    }

    /// wxl.ui.get_cursor_screen_pos() -> x, y: cursor position in absolute screen space (matches the
    /// draw-list coordinate system).
    inline int L_getCursorScreenPos(lua_State* L)
    {
        if (!InDraw(L, "get_cursor_screen_pos")) return 0;
        return PushVec2(L, ImGui::GetCursorScreenPos());
    }

    /// wxl.ui.set_cursor_screen_pos(x, y): sets the cursor position in absolute screen space.
    inline int L_setCursorScreenPos(lua_State* L)
    {
        if (!InDraw(L, "set_cursor_screen_pos")) return 0;
        const float x = static_cast<float>(luaL_checknumber(L, 1));
        const float y = static_cast<float>(luaL_checknumber(L, 2));
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        return 0;
    }

    /// wxl.ui.align_text_to_frame_padding(): aligns the next text baseline to framed-widget height.
    inline int L_alignTextToFramePadding(lua_State* L)
    {
        if (!InDraw(L, "align_text_to_frame_padding")) return 0;
        ImGui::AlignTextToFramePadding();
        return 0;
    }

    /// wxl.ui.get_frame_height() -> h: height of a framed widget (~ font size + frame padding).
    inline int L_getFrameHeight(lua_State* L)
    {
        if (!InDraw(L, "get_frame_height")) return 0;
        lua_pushnumber(L, ImGui::GetFrameHeight());
        return 1;
    }

    /// wxl.ui.get_frame_height_with_spacing() -> h: framed-widget height plus item spacing.
    inline int L_getFrameHeightWithSpacing(lua_State* L)
    {
        if (!InDraw(L, "get_frame_height_with_spacing")) return 0;
        lua_pushnumber(L, ImGui::GetFrameHeightWithSpacing());
        return 1;
    }

    /// Adds the layout fields to the ui subtable on top of the stack.
    inline void RegisterLayout(lua_State* L)
    {
        lua_pushcfunction(L, &L_separator);                lua_setfield(L, -2, "separator");
        lua_pushcfunction(L, &L_separatorText);            lua_setfield(L, -2, "separator_text");
        lua_pushcfunction(L, &L_sameLine);                 lua_setfield(L, -2, "same_line");
        lua_pushcfunction(L, &L_newLine);                  lua_setfield(L, -2, "new_line");
        lua_pushcfunction(L, &L_spacing);                  lua_setfield(L, -2, "spacing");
        lua_pushcfunction(L, &L_dummy);                    lua_setfield(L, -2, "dummy");
        lua_pushcfunction(L, &L_indent);                   lua_setfield(L, -2, "indent");
        lua_pushcfunction(L, &L_unindent);                 lua_setfield(L, -2, "unindent");
        lua_pushcfunction(L, &L_beginGroup);               lua_setfield(L, -2, "begin_group");
        lua_pushcfunction(L, &L_endGroup);                 lua_setfield(L, -2, "end_group");
        lua_pushcfunction(L, &L_getCursorPos);             lua_setfield(L, -2, "get_cursor_pos");
        lua_pushcfunction(L, &L_setCursorPos);             lua_setfield(L, -2, "set_cursor_pos");
        lua_pushcfunction(L, &L_getCursorScreenPos);       lua_setfield(L, -2, "get_cursor_screen_pos");
        lua_pushcfunction(L, &L_setCursorScreenPos);       lua_setfield(L, -2, "set_cursor_screen_pos");
        lua_pushcfunction(L, &L_alignTextToFramePadding);  lua_setfield(L, -2, "align_text_to_frame_padding");
        lua_pushcfunction(L, &L_getFrameHeight);           lua_setfield(L, -2, "get_frame_height");
        lua_pushcfunction(L, &L_getFrameHeightWithSpacing);lua_setfield(L, -2, "get_frame_height_with_spacing");
    }
}
