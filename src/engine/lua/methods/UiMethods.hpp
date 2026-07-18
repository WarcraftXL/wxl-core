// UI method context: the wxl.ui.* immediate-mode surface over Dear ImGui. Thin aggregator of ui/*.hpp.
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
#include "engine/lua/methods/ui/Windows.hpp"
#include "engine/lua/methods/ui/Layout.hpp"
#include "engine/lua/methods/ui/Text.hpp"
#include "engine/lua/methods/ui/Widgets.hpp"
#include "engine/lua/methods/ui/Drags.hpp"
#include "engine/lua/methods/ui/Input.hpp"
#include "engine/lua/methods/ui/Combo.hpp"
#include "engine/lua/methods/ui/Trees.hpp"
#include "engine/lua/methods/ui/TabsMenus.hpp"
#include "engine/lua/methods/ui/Popups.hpp"
#include "engine/lua/methods/ui/Tables.hpp"
#include "engine/lua/methods/ui/DrawList.hpp"
#include "engine/lua/methods/ui/Style.hpp"
#include "engine/lua/methods/ui/Query.hpp"
#include "engine/lua/methods/ui/Constants.hpp"

/// The wxl.ui.* surface: a comprehensive, Lua-idiomatic binding of Dear ImGui's immediate-mode API. A Lua
/// extension calls these to describe a window, re-run every frame. This file is only the aggregator: the
/// actual bindings live in ui/*.hpp (windows, layout, text, widgets, drags/sliders, input, combos, trees,
/// tabs/menus, popups, tables, draw lists, style, queries) and Register() wires them all onto one `ui`
/// subtable of the `wxl` table on the stack. Flag-taking calls read integer masks from the constant tables
/// (wxl.ui.WINDOW, .COL, .TREE, .TABLE, ...); modders combine flags with LuaJIT's bit.bor. Header-only.
///
/// CONTRACT: every wxl.ui.* call is valid ONLY inside a handler of the 'draw' event (wxl.on("draw", ...)),
/// which the ImGui host fires once per frame with the frame open. A call from anywhere else raises a Lua
/// error rather than corrupting ImGui state. `end` is a Lua keyword, so the window closer is wxl.ui.finish
/// (and the paired closers for child/combo/menu/etc. are end_child, end_combo, end_menu, ...).
namespace wxl::lua::methods::ui
{
    /**
     * @brief Adds the `ui` subtable (wxl.ui.*) to the table on top of the stack. Stack-neutral.
     * @param L  the engine lua_State, with the target `wxl` table at index -1.
     */
    inline void Register(lua_State* L)
    {
        lua_newtable(L);            // the wxl.ui subtable
        RegisterDrawListMeta(L);    // create the handle metatable before DrawList fills it (stack-neutral)

        RegisterCommon(L);
        RegisterWindows(L);
        RegisterLayout(L);
        RegisterText(L);
        RegisterWidgets(L);
        RegisterDrags(L);
        RegisterInput(L);
        RegisterCombo(L);
        RegisterTrees(L);
        RegisterTabsMenus(L);
        RegisterPopups(L);
        RegisterTables(L);
        RegisterDrawList(L);
        RegisterStyle(L);
        RegisterQuery(L);
        RegisterConstants(L);

        lua_setfield(L, -2, "ui");  // wxl.ui = subtable
    }
}
