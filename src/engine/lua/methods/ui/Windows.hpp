// UI windows: begin/finish, child regions and next-window / current-window state for wxl.ui.*.
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

#include "engine/lua/Marshal.hpp"
#include "engine/lua/methods/ui/Common.hpp"

/// Window management: opening/closing windows and child regions, positioning the next window, and querying
/// the current window's geometry and focus state.
namespace wxl::lua::methods::ui
{
    /// wxl.ui.begin(title[, flags]) -> visible: opens a window. visible is false when the window is
    /// collapsed or clipped; a script may skip building its body then. wxl.ui.finish MUST still be called.
    inline int L_begin(lua_State* L)
    {
        if (!InDraw(L, "begin")) return 0;
        const char* title = luaL_checkstring(L, 1);
        lua_pushboolean(L, ImGui::Begin(title, nullptr, OptFlags(L, 2)));
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

    /// wxl.ui.begin_child(id[, w, h[, border]]) -> visible: opens a scrollable child region. As with a
    /// window, end_child MUST always be called. w/h of 0 means "use remaining space" on that axis.
    inline int L_beginChild(lua_State* L)
    {
        if (!InDraw(L, "begin_child")) return 0;
        const char*     id = luaL_checkstring(L, 1);
        const ImVec2    sz(OptFloat(L, 2, 0.0f), OptFloat(L, 3, 0.0f));
        ImGuiChildFlags cf = OptBool(L, 4, false) ? ImGuiChildFlags_Borders : ImGuiChildFlags_None;
        lua_pushboolean(L, ImGui::BeginChild(id, sz, cf));
        return 1;
    }

    /// wxl.ui.end_child(): closes the region opened by begin_child. Always call it.
    inline int L_endChild(lua_State* L)
    {
        if (!InDraw(L, "end_child")) return 0;
        ImGui::EndChild();
        return 0;
    }

    /// wxl.ui.set_next_window_pos(x, y[, cond]): positions the next window (call before begin). cond is one
    /// of wxl.ui.COND.* (default: always).
    inline int L_setNextWindowPos(lua_State* L)
    {
        if (!InDraw(L, "set_next_window_pos")) return 0;
        const float x = static_cast<float>(luaL_checknumber(L, 1));
        const float y = static_cast<float>(luaL_checknumber(L, 2));
        ImGui::SetNextWindowPos(ImVec2(x, y), OptInt(L, 3, 0));
        return 0;
    }

    /// wxl.ui.set_next_window_size(w, h[, cond]): sizes the next window (call before begin). An axis of 0
    /// auto-fits. cond is one of wxl.ui.COND.* (default: always).
    inline int L_setNextWindowSize(lua_State* L)
    {
        if (!InDraw(L, "set_next_window_size")) return 0;
        const float w = static_cast<float>(luaL_checknumber(L, 1));
        const float h = static_cast<float>(luaL_checknumber(L, 2));
        ImGui::SetNextWindowSize(ImVec2(w, h), OptInt(L, 3, 0));
        return 0;
    }

    /// wxl.ui.set_next_window_collapsed(collapsed[, cond]): sets the next window's collapsed state.
    inline int L_setNextWindowCollapsed(lua_State* L)
    {
        if (!InDraw(L, "set_next_window_collapsed")) return 0;
        ImGui::SetNextWindowCollapsed(lua_toboolean(L, 1) != 0, OptInt(L, 2, 0));
        return 0;
    }

    /// wxl.ui.set_next_window_focus(): brings the next window to front / focus.
    inline int L_setNextWindowFocus(lua_State* L)
    {
        if (!InDraw(L, "set_next_window_focus")) return 0;
        ImGui::SetNextWindowFocus();
        return 0;
    }

    /// wxl.ui.set_next_window_bg_alpha(alpha): overrides the next window's background alpha (0..1).
    inline int L_setNextWindowBgAlpha(lua_State* L)
    {
        if (!InDraw(L, "set_next_window_bg_alpha")) return 0;
        ImGui::SetNextWindowBgAlpha(static_cast<float>(luaL_checknumber(L, 1)));
        return 0;
    }

    /// wxl.ui.get_window_pos() -> x, y: current window position in screen space.
    inline int L_getWindowPos(lua_State* L)
    {
        if (!InDraw(L, "get_window_pos")) return 0;
        return PushVec2(L, ImGui::GetWindowPos());
    }

    /// wxl.ui.get_window_size() -> w, h: current window size.
    inline int L_getWindowSize(lua_State* L)
    {
        if (!InDraw(L, "get_window_size")) return 0;
        return PushVec2(L, ImGui::GetWindowSize());
    }

    /// wxl.ui.get_content_region_avail() -> w, h: space left from the cursor to the window's inner edge.
    inline int L_getContentRegionAvail(lua_State* L)
    {
        if (!InDraw(L, "get_content_region_avail")) return 0;
        return PushVec2(L, ImGui::GetContentRegionAvail());
    }

    /// wxl.ui.is_window_focused([flags]) -> focused: whether the current window is focused. flags are
    /// wxl.ui.FOCUSED.* (default 0: this exact window).
    inline int L_isWindowFocused(lua_State* L)
    {
        if (!InDraw(L, "is_window_focused")) return 0;
        lua_pushboolean(L, ImGui::IsWindowFocused(OptFlags(L, 1)));
        return 1;
    }

    /// wxl.ui.is_window_hovered([flags]) -> hovered: whether the current window is hovered. flags are
    /// wxl.ui.HOVERED.* (default 0).
    inline int L_isWindowHovered(lua_State* L)
    {
        if (!InDraw(L, "is_window_hovered")) return 0;
        lua_pushboolean(L, ImGui::IsWindowHovered(OptFlags(L, 1)));
        return 1;
    }

    /// wxl.ui.set_window_font_scale(scale): per-window font scale multiplier for the current window.
    inline int L_setWindowFontScale(lua_State* L)
    {
        if (!InDraw(L, "set_window_font_scale")) return 0;
        ImGui::SetWindowFontScale(static_cast<float>(luaL_checknumber(L, 1)));
        return 0;
    }

    /// Adds the window-management fields to the ui subtable on top of the stack.
    inline void RegisterWindows(lua_State* L)
    {
        static const luaL_Reg fns[] = {
            { "begin",                    L_begin },
            { "finish",                   L_finish },
            { "begin_child",              L_beginChild },
            { "end_child",                L_endChild },
            { "set_next_window_pos",       L_setNextWindowPos },
            { "set_next_window_size",      L_setNextWindowSize },
            { "set_next_window_collapsed", L_setNextWindowCollapsed },
            { "set_next_window_focus",     L_setNextWindowFocus },
            { "set_next_window_bg_alpha",  L_setNextWindowBgAlpha },
            { "get_window_pos",            L_getWindowPos },
            { "get_window_size",           L_getWindowSize },
            { "get_content_region_avail",  L_getContentRegionAvail },
            { "is_window_focused",         L_isWindowFocused },
            { "is_window_hovered",         L_isWindowHovered },
            { "set_window_font_scale",     L_setWindowFontScale },
            { nullptr, nullptr },
        };
        SetFunctions(L, fns);
    }
}
