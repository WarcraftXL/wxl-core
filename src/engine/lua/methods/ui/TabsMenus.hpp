// UI tabs & menus: tab bars, menu bars and menu items for wxl.ui.*.
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

/// Tab bars and menus. Each begin_* that returns true has a matching end_* that must be called only then,
/// except the menu-bar begins which follow ImGui's "call end only if begin returned true" rule.
namespace wxl::lua::methods::ui
{
    /// wxl.ui.begin_tab_bar(id[, flags]) -> open: opens a tab bar. Call end_tab_bar only when true.
    inline int L_beginTabBar(lua_State* L)
    {
        if (!InDraw(L, "begin_tab_bar")) return 0;
        lua_pushboolean(L, ImGui::BeginTabBar(luaL_checkstring(L, 1), OptFlags(L, 2)));
        return 1;
    }

    /// wxl.ui.end_tab_bar(): closes the tab bar (call only when begin_tab_bar returned true).
    inline int L_endTabBar(lua_State* L)
    {
        if (!InDraw(L, "end_tab_bar")) return 0;
        ImGui::EndTabBar();
        return 0;
    }

    /// wxl.ui.begin_tab_item(label[, flags]) -> selected: adds a tab. When true, emit its body and call
    /// end_tab_item.
    inline int L_beginTabItem(lua_State* L)
    {
        if (!InDraw(L, "begin_tab_item")) return 0;
        lua_pushboolean(L, ImGui::BeginTabItem(luaL_checkstring(L, 1), nullptr, OptFlags(L, 2)));
        return 1;
    }

    /// wxl.ui.end_tab_item(): closes the tab body (call only when begin_tab_item returned true).
    inline int L_endTabItem(lua_State* L)
    {
        if (!InDraw(L, "end_tab_item")) return 0;
        ImGui::EndTabItem();
        return 0;
    }

    /// wxl.ui.begin_menu_bar() -> open: appends to the current window's menu bar (needs the MenuBar window
    /// flag). Call end_menu_bar only when true.
    inline int L_beginMenuBar(lua_State* L)
    {
        if (!InDraw(L, "begin_menu_bar")) return 0;
        lua_pushboolean(L, ImGui::BeginMenuBar());
        return 1;
    }

    /// wxl.ui.end_menu_bar(): closes the menu bar (call only when begin_menu_bar returned true).
    inline int L_endMenuBar(lua_State* L)
    {
        if (!InDraw(L, "end_menu_bar")) return 0;
        ImGui::EndMenuBar();
        return 0;
    }

    /// wxl.ui.begin_main_menu_bar() -> open: opens the full-screen main menu bar. Call end_main_menu_bar
    /// only when true.
    inline int L_beginMainMenuBar(lua_State* L)
    {
        if (!InDraw(L, "begin_main_menu_bar")) return 0;
        lua_pushboolean(L, ImGui::BeginMainMenuBar());
        return 1;
    }

    /// wxl.ui.end_main_menu_bar(): closes the main menu bar (call only when it returned true).
    inline int L_endMainMenuBar(lua_State* L)
    {
        if (!InDraw(L, "end_main_menu_bar")) return 0;
        ImGui::EndMainMenuBar();
        return 0;
    }

    /// wxl.ui.begin_menu(label[, enabled]) -> open: a sub-menu entry. When true, emit its items and call
    /// end_menu. enabled defaults to true.
    inline int L_beginMenu(lua_State* L)
    {
        if (!InDraw(L, "begin_menu")) return 0;
        lua_pushboolean(L, ImGui::BeginMenu(luaL_checkstring(L, 1), OptBool(L, 2, true)));
        return 1;
    }

    /// wxl.ui.end_menu(): closes a menu (call only when begin_menu returned true).
    inline int L_endMenu(lua_State* L)
    {
        if (!InDraw(L, "end_menu")) return 0;
        ImGui::EndMenu();
        return 0;
    }

    /// wxl.ui.menu_item(label[, shortcut[, selected[, enabled]]]) -> clicked: a menu row. shortcut is a
    /// display-only hint; selected shows a check; enabled defaults to true.
    inline int L_menuItem(lua_State* L)
    {
        if (!InDraw(L, "menu_item")) return 0;
        const char* label    = luaL_checkstring(L, 1);
        const char* shortcut = luaL_optstring(L, 2, nullptr);
        const bool  selected = OptBool(L, 3, false);
        const bool  enabled  = OptBool(L, 4, true);
        lua_pushboolean(L, ImGui::MenuItem(label, shortcut, selected, enabled));
        return 1;
    }

    /// Adds the tab/menu fields to the ui subtable on top of the stack.
    inline void RegisterTabsMenus(lua_State* L)
    {
        lua_pushcfunction(L, &L_beginTabBar);      lua_setfield(L, -2, "begin_tab_bar");
        lua_pushcfunction(L, &L_endTabBar);        lua_setfield(L, -2, "end_tab_bar");
        lua_pushcfunction(L, &L_beginTabItem);     lua_setfield(L, -2, "begin_tab_item");
        lua_pushcfunction(L, &L_endTabItem);       lua_setfield(L, -2, "end_tab_item");
        lua_pushcfunction(L, &L_beginMenuBar);     lua_setfield(L, -2, "begin_menu_bar");
        lua_pushcfunction(L, &L_endMenuBar);       lua_setfield(L, -2, "end_menu_bar");
        lua_pushcfunction(L, &L_beginMainMenuBar); lua_setfield(L, -2, "begin_main_menu_bar");
        lua_pushcfunction(L, &L_endMainMenuBar);   lua_setfield(L, -2, "end_main_menu_bar");
        lua_pushcfunction(L, &L_beginMenu);        lua_setfield(L, -2, "begin_menu");
        lua_pushcfunction(L, &L_endMenu);          lua_setfield(L, -2, "end_menu");
        lua_pushcfunction(L, &L_menuItem);         lua_setfield(L, -2, "menu_item");
    }
}
