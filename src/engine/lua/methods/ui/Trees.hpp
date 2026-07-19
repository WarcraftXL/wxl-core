// UI trees: tree nodes and collapsing headers for wxl.ui.*.
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

/// Collapsible hierarchy: tree nodes (each open node needs a matching tree_pop) and collapsing headers
/// (which do not). Flags are wxl.ui.TREE.*.
namespace wxl::lua::methods::ui
{
    /// wxl.ui.tree_node(label[, flags]) -> open: a collapsible node. When it returns true, emit the child
    /// content and call tree_pop.
    inline int L_treeNode(lua_State* L)
    {
        if (!InDraw(L, "tree_node")) return 0;
        const char* label = luaL_checkstring(L, 1);
        lua_pushboolean(L, ImGui::TreeNodeEx(label, OptFlags(L, 2)));
        return 1;
    }

    /// wxl.ui.tree_pop(): closes the indentation/id scope of an open tree_node (call only when it was open).
    inline int L_treePop(lua_State* L)
    {
        if (!InDraw(L, "tree_pop")) return 0;
        ImGui::TreePop();
        return 0;
    }

    /// wxl.ui.tree_push(id): manually pushes a tree scope (indent + id) without drawing a node.
    inline int L_treePush(lua_State* L)
    {
        if (!InDraw(L, "tree_push")) return 0;
        ImGui::TreePush(luaL_checkstring(L, 1));
        return 0;
    }

    /// wxl.ui.collapsing_header(label[, flags]) -> open: a header that toggles a section. Unlike tree_node
    /// it does not indent nor require a pop.
    inline int L_collapsingHeader(lua_State* L)
    {
        if (!InDraw(L, "collapsing_header")) return 0;
        const char* label = luaL_checkstring(L, 1);
        lua_pushboolean(L, ImGui::CollapsingHeader(label, OptFlags(L, 2)));
        return 1;
    }

    /// wxl.ui.set_next_item_open(open[, cond]): sets the open state of the next tree_node/collapsing_header.
    /// cond is wxl.ui.COND.* (default: always).
    inline int L_setNextItemOpen(lua_State* L)
    {
        if (!InDraw(L, "set_next_item_open")) return 0;
        ImGui::SetNextItemOpen(lua_toboolean(L, 1) != 0, OptInt(L, 2, 0));
        return 0;
    }

    /// Adds the tree fields to the ui subtable on top of the stack.
    inline void RegisterTrees(lua_State* L)
    {
        static const luaL_Reg fns[] = {
            { "tree_node",         L_treeNode },
            { "tree_pop",          L_treePop },
            { "tree_push",         L_treePush },
            { "collapsing_header", L_collapsingHeader },
            { "set_next_item_open",L_setNextItemOpen },
            { nullptr, nullptr },
        };
        SetFunctions(L, fns);
    }
}
