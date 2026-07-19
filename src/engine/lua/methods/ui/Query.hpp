// UI query: last-item state, mouse state and misc frame queries for wxl.ui.*.
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

/// Interaction queries: state of the last submitted item, mouse buttons/position, timing, and text sizing.
/// Mouse buttons are wxl.ui.MOUSE.* (left/right/middle); item-hover flags are wxl.ui.HOVERED.*.
namespace wxl::lua::methods::ui
{
    /// wxl.ui.is_item_hovered([flags]) -> hovered: whether the last item is hovered. flags are wxl.ui.HOVERED.*.
    inline int L_isItemHovered(lua_State* L)
    {
        if (!InDraw(L, "is_item_hovered")) return 0;
        lua_pushboolean(L, ImGui::IsItemHovered(OptFlags(L, 1)));
        return 1;
    }

    /// wxl.ui.is_item_active() -> active: whether the last item is being interacted with (e.g. held).
    inline int L_isItemActive(lua_State* L)
    {
        if (!InDraw(L, "is_item_active")) return 0;
        lua_pushboolean(L, ImGui::IsItemActive());
        return 1;
    }

    /// wxl.ui.is_item_clicked([button]) -> clicked: whether the last item was clicked. button is wxl.ui.MOUSE.*.
    inline int L_isItemClicked(lua_State* L)
    {
        if (!InDraw(L, "is_item_clicked")) return 0;
        lua_pushboolean(L, ImGui::IsItemClicked(OptInt(L, 1, 0)));
        return 1;
    }

    /// wxl.ui.is_item_edited() -> edited: whether the last item changed its value this frame.
    inline int L_isItemEdited(lua_State* L)
    {
        if (!InDraw(L, "is_item_edited")) return 0;
        lua_pushboolean(L, ImGui::IsItemEdited());
        return 1;
    }

    /// wxl.ui.is_item_focused() -> focused: whether the last item is focused for keyboard/gamepad nav.
    inline int L_isItemFocused(lua_State* L)
    {
        if (!InDraw(L, "is_item_focused")) return 0;
        lua_pushboolean(L, ImGui::IsItemFocused());
        return 1;
    }

    /// wxl.ui.is_any_item_hovered() -> hovered: whether any item is hovered this frame.
    inline int L_isAnyItemHovered(lua_State* L)
    {
        if (!InDraw(L, "is_any_item_hovered")) return 0;
        lua_pushboolean(L, ImGui::IsAnyItemHovered());
        return 1;
    }

    /// wxl.ui.is_mouse_clicked(button[, repeat]) -> clicked: mouse button just pressed. button is wxl.ui.MOUSE.*.
    inline int L_isMouseClicked(lua_State* L)
    {
        if (!InDraw(L, "is_mouse_clicked")) return 0;
        lua_pushboolean(L, ImGui::IsMouseClicked(static_cast<ImGuiMouseButton>(luaL_checkinteger(L, 1)), OptBool(L, 2, false)));
        return 1;
    }

    /// wxl.ui.is_mouse_double_clicked(button) -> clicked: mouse button just double-clicked.
    inline int L_isMouseDoubleClicked(lua_State* L)
    {
        if (!InDraw(L, "is_mouse_double_clicked")) return 0;
        lua_pushboolean(L, ImGui::IsMouseDoubleClicked(static_cast<ImGuiMouseButton>(luaL_checkinteger(L, 1))));
        return 1;
    }

    /// wxl.ui.is_mouse_down(button) -> down: mouse button currently held.
    inline int L_isMouseDown(lua_State* L)
    {
        if (!InDraw(L, "is_mouse_down")) return 0;
        lua_pushboolean(L, ImGui::IsMouseDown(static_cast<ImGuiMouseButton>(luaL_checkinteger(L, 1))));
        return 1;
    }

    /// wxl.ui.is_mouse_dragging(button) -> dragging: mouse dragging with the given button held.
    inline int L_isMouseDragging(lua_State* L)
    {
        if (!InDraw(L, "is_mouse_dragging")) return 0;
        lua_pushboolean(L, ImGui::IsMouseDragging(static_cast<ImGuiMouseButton>(luaL_checkinteger(L, 1))));
        return 1;
    }

    /// wxl.ui.get_mouse_pos() -> x, y: mouse position in screen space.
    inline int L_getMousePos(lua_State* L)
    {
        if (!InDraw(L, "get_mouse_pos")) return 0;
        return PushVec2(L, ImGui::GetMousePos());
    }

    /// wxl.ui.get_mouse_drag_delta([button]) -> x, y: drag delta from the press point. button is wxl.ui.MOUSE.*.
    inline int L_getMouseDragDelta(lua_State* L)
    {
        if (!InDraw(L, "get_mouse_drag_delta")) return 0;
        return PushVec2(L, ImGui::GetMouseDragDelta(static_cast<ImGuiMouseButton>(OptInt(L, 1, 0))));
    }

    /// wxl.ui.get_time() -> seconds: the global ImGui time, incremented each frame by the delta time.
    inline int L_getTime(lua_State* L)
    {
        if (!InDraw(L, "get_time")) return 0;
        lua_pushnumber(L, ImGui::GetTime());
        return 1;
    }

    /// wxl.ui.get_frame_count() -> n: the global ImGui frame counter.
    inline int L_getFrameCount(lua_State* L)
    {
        if (!InDraw(L, "get_frame_count")) return 0;
        lua_pushinteger(L, ImGui::GetFrameCount());
        return 1;
    }

    /// wxl.ui.calc_text_size(s) -> w, h: the size the given text would occupy with the current font.
    inline int L_calcTextSize(lua_State* L)
    {
        if (!InDraw(L, "calc_text_size")) return 0;
        return PushVec2(L, ImGui::CalcTextSize(luaL_checkstring(L, 1)));
    }

    /// wxl.ui.set_keyboard_focus_here([offset]): focuses the keyboard on the next widget (offset selects a
    /// sub-component; -1 targets the previous widget).
    inline int L_setKeyboardFocusHere(lua_State* L)
    {
        if (!InDraw(L, "set_keyboard_focus_here")) return 0;
        ImGui::SetKeyboardFocusHere(OptInt(L, 1, 0));
        return 0;
    }

    /// wxl.ui.set_item_default_focus(): makes the last item the default focus of a newly appearing window.
    inline int L_setItemDefaultFocus(lua_State* L)
    {
        if (!InDraw(L, "set_item_default_focus")) return 0;
        ImGui::SetItemDefaultFocus();
        return 0;
    }

    /// Adds the query fields to the ui subtable on top of the stack.
    inline void RegisterQuery(lua_State* L)
    {
        static const luaL_Reg fns[] = {
            { "is_item_hovered",         L_isItemHovered },
            { "is_item_active",          L_isItemActive },
            { "is_item_clicked",         L_isItemClicked },
            { "is_item_edited",          L_isItemEdited },
            { "is_item_focused",         L_isItemFocused },
            { "is_any_item_hovered",     L_isAnyItemHovered },
            { "is_mouse_clicked",        L_isMouseClicked },
            { "is_mouse_double_clicked", L_isMouseDoubleClicked },
            { "is_mouse_down",           L_isMouseDown },
            { "is_mouse_dragging",       L_isMouseDragging },
            { "get_mouse_pos",           L_getMousePos },
            { "get_mouse_drag_delta",    L_getMouseDragDelta },
            { "get_time",                L_getTime },
            { "get_frame_count",         L_getFrameCount },
            { "calc_text_size",          L_calcTextSize },
            { "set_keyboard_focus_here", L_setKeyboardFocusHere },
            { "set_item_default_focus",  L_setItemDefaultFocus },
            { nullptr, nullptr },
        };
        SetFunctions(L, fns);
    }
}
