// UI popups & tooltips: modal/non-modal popups, context popups and tooltips for wxl.ui.*.
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

/// Popups and tooltips. A popup is armed with open_popup(id) then shown by a begin_popup*(id) that returns
/// true; end_popup is called only when it did. Tooltips follow the same begin/end-when-true rule.
namespace wxl::lua::methods::ui
{
    /// wxl.ui.open_popup(id): marks the popup with this id as open (call once on the triggering event, not
    /// every frame).
    inline int L_openPopup(lua_State* L)
    {
        if (!InDraw(L, "open_popup")) return 0;
        ImGui::OpenPopup(luaL_checkstring(L, 1));
        return 0;
    }

    /// wxl.ui.begin_popup(id[, flags]) -> open: begins a non-modal popup. When true, emit its body and call
    /// end_popup.
    inline int L_beginPopup(lua_State* L)
    {
        if (!InDraw(L, "begin_popup")) return 0;
        lua_pushboolean(L, ImGui::BeginPopup(luaL_checkstring(L, 1), OptFlags(L, 2)));
        return 1;
    }

    /// wxl.ui.begin_popup_modal(title[, flags]) -> open: begins a modal popup (blocks the rest of the UI).
    /// When true, emit its body and call end_popup.
    inline int L_beginPopupModal(lua_State* L)
    {
        if (!InDraw(L, "begin_popup_modal")) return 0;
        lua_pushboolean(L, ImGui::BeginPopupModal(luaL_checkstring(L, 1), nullptr, OptFlags(L, 2)));
        return 1;
    }

    /// wxl.ui.begin_popup_context_item([id[, flags]]) -> open: opens a right-click context popup for the
    /// last item. id may be nil to bind to the previous item. When true, emit its body and call end_popup.
    inline int L_beginPopupContextItem(lua_State* L)
    {
        if (!InDraw(L, "begin_popup_context_item")) return 0;
        const char* id = luaL_optstring(L, 1, nullptr);
        lua_pushboolean(L, ImGui::BeginPopupContextItem(id, OptInt(L, 2, ImGuiPopupFlags_MouseButtonRight)));
        return 1;
    }

    /// wxl.ui.end_popup(): closes a popup (call only when its begin_popup* returned true).
    inline int L_endPopup(lua_State* L)
    {
        if (!InDraw(L, "end_popup")) return 0;
        ImGui::EndPopup();
        return 0;
    }

    /// wxl.ui.close_current_popup(): closes the popup we are currently building into.
    inline int L_closeCurrentPopup(lua_State* L)
    {
        if (!InDraw(L, "close_current_popup")) return 0;
        ImGui::CloseCurrentPopup();
        return 0;
    }

    /// wxl.ui.begin_tooltip() -> open: begins a tooltip window. When true, emit its body and call
    /// end_tooltip.
    inline int L_beginTooltip(lua_State* L)
    {
        if (!InDraw(L, "begin_tooltip")) return 0;
        lua_pushboolean(L, ImGui::BeginTooltip());
        return 1;
    }

    /// wxl.ui.end_tooltip(): closes a tooltip (call only when begin_tooltip returned true).
    inline int L_endTooltip(lua_State* L)
    {
        if (!InDraw(L, "end_tooltip")) return 0;
        ImGui::EndTooltip();
        return 0;
    }

    /// wxl.ui.set_tooltip(s): a text-only tooltip, typically after an is_item_hovered() check.
    inline int L_setTooltip(lua_State* L)
    {
        if (!InDraw(L, "set_tooltip")) return 0;
        ImGui::SetTooltip("%s", luaL_checkstring(L, 1));
        return 0;
    }

    /// Adds the popup/tooltip fields to the ui subtable on top of the stack.
    inline void RegisterPopups(lua_State* L)
    {
        static const luaL_Reg fns[] = {
            { "open_popup",                L_openPopup },
            { "begin_popup",               L_beginPopup },
            { "begin_popup_modal",         L_beginPopupModal },
            { "begin_popup_context_item",  L_beginPopupContextItem },
            { "end_popup",                 L_endPopup },
            { "close_current_popup",       L_closeCurrentPopup },
            { "begin_tooltip",             L_beginTooltip },
            { "end_tooltip",               L_endTooltip },
            { "set_tooltip",               L_setTooltip },
            { nullptr, nullptr },
        };
        SetFunctions(L, fns);
    }
}
