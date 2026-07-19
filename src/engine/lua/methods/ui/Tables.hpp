// UI tables: the multi-column table API (ImGui >= 1.80) for wxl.ui.*.
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

/// Multi-column tables. A table is begin_table(...)/end_table(); rows advance with table_next_row and
/// columns with table_next_column or table_set_column_index (1-based). Flags are wxl.ui.TABLE.* and
/// wxl.ui.TABLE_COLUMN.*.
namespace wxl::lua::methods::ui
{
    /// wxl.ui.begin_table(id, columns[, flags[, w, h]]) -> open: begins a table with the given column
    /// count. When true, build rows/cells and call end_table. w/h size the outer container (0: auto).
    inline int L_beginTable(lua_State* L)
    {
        if (!InDraw(L, "begin_table")) return 0;
        const char*  id   = luaL_checkstring(L, 1);
        const int    cols = static_cast<int>(luaL_checkinteger(L, 2));
        const int    flags = OptFlags(L, 3);
        const ImVec2 outer(OptFloat(L, 4, 0.0f), OptFloat(L, 5, 0.0f));
        lua_pushboolean(L, ImGui::BeginTable(id, cols, flags, outer));
        return 1;
    }

    /// wxl.ui.end_table(): closes a table (call only when begin_table returned true).
    inline int L_endTable(lua_State* L)
    {
        if (!InDraw(L, "end_table")) return 0;
        ImGui::EndTable();
        return 0;
    }

    /// wxl.ui.table_next_row([flags[, min_h]]): starts a new row. flags are wxl.ui.TABLE_ROW.*; min_h is the
    /// minimum row height.
    inline int L_tableNextRow(lua_State* L)
    {
        if (!InDraw(L, "table_next_row")) return 0;
        ImGui::TableNextRow(OptFlags(L, 1), OptFloat(L, 2, 0.0f));
        return 0;
    }

    /// wxl.ui.table_next_column() -> visible: moves to the next column (wrapping to a new row after the
    /// last). visible is false when the column is clipped.
    inline int L_tableNextColumn(lua_State* L)
    {
        if (!InDraw(L, "table_next_column")) return 0;
        lua_pushboolean(L, ImGui::TableNextColumn());
        return 1;
    }

    /// wxl.ui.table_set_column_index(i) -> visible: moves to column i (1-based). visible is false when the
    /// column is clipped.
    inline int L_tableSetColumnIndex(lua_State* L)
    {
        if (!InDraw(L, "table_set_column_index")) return 0;
        const int i = static_cast<int>(luaL_checkinteger(L, 1)) - 1; // 1-based Lua -> 0-based ImGui
        lua_pushboolean(L, ImGui::TableSetColumnIndex(i));
        return 1;
    }

    /// wxl.ui.table_setup_column(label[, flags[, init_w]]): declares a column for the header row and layout.
    /// flags are wxl.ui.TABLE_COLUMN.*; init_w is the initial width or weight.
    inline int L_tableSetupColumn(lua_State* L)
    {
        if (!InDraw(L, "table_setup_column")) return 0;
        const char* label = luaL_checkstring(L, 1);
        ImGui::TableSetupColumn(label, OptFlags(L, 2), OptFloat(L, 3, 0.0f));
        return 0;
    }

    /// wxl.ui.table_headers_row(): submits the header row built from the table_setup_column declarations.
    inline int L_tableHeadersRow(lua_State* L)
    {
        if (!InDraw(L, "table_headers_row")) return 0;
        ImGui::TableHeadersRow();
        return 0;
    }

    /// wxl.ui.table_setup_scroll_freeze(cols, rows): keeps the first cols columns and rows rows pinned when
    /// the table scrolls.
    inline int L_tableSetupScrollFreeze(lua_State* L)
    {
        if (!InDraw(L, "table_setup_scroll_freeze")) return 0;
        const int cols = static_cast<int>(luaL_checkinteger(L, 1));
        const int rows = static_cast<int>(luaL_checkinteger(L, 2));
        ImGui::TableSetupScrollFreeze(cols, rows);
        return 0;
    }

    /// Adds the table fields to the ui subtable on top of the stack.
    inline void RegisterTables(lua_State* L)
    {
        static const luaL_Reg fns[] = {
            { "begin_table",               L_beginTable },
            { "end_table",                 L_endTable },
            { "table_next_row",            L_tableNextRow },
            { "table_next_column",         L_tableNextColumn },
            { "table_set_column_index",    L_tableSetColumnIndex },
            { "table_setup_column",        L_tableSetupColumn },
            { "table_headers_row",         L_tableHeadersRow },
            { "table_setup_scroll_freeze", L_tableSetupScrollFreeze },
            { nullptr, nullptr },
        };
        SetFunctions(L, fns);
    }
}
