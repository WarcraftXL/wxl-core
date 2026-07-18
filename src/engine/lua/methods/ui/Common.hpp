// UI shared helpers: the frame guard, color marshalling and the ImDrawList handle used across wxl.ui.*.
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

/// Shared plumbing for the split wxl.ui.* binding: the draw-frame guard, small argument helpers, color
/// marshalling (Lua floats <-> ImVec4/ImU32) and the ImDrawList* handle. Every ui/*.hpp includes this and
/// nothing else, so the helpers stay in one place with no duplication.
namespace wxl::lua::methods::ui
{
    /// Metatable name backing the draw-list handle returned by wxl.ui.window_draw_list() and friends.
    inline constexpr const char* kDrawListMeta = "wxl.ui.drawlist";

    /// Rejects a call made outside the open ImGui frame. On failure it longjmps out via luaL_error and
    /// never returns; callers use it as `if (!InDraw(L, name)) return 0;` purely for readability.
    inline bool InDraw(lua_State* L, const char* fn)
    {
        if (wxl::lua::ui::InFrame())
            return true;
        luaL_error(L, "wxl.ui.%s: only valid inside a 'draw' handler (wxl.on(\"draw\", ...))", fn);
        return false; // unreachable: luaL_error does not return
    }

    // --- small argument helpers (optional args with sane defaults) -----------------------------------

    /// Optional float at stack index i, defaulting to d when absent/nil.
    inline float OptFloat(lua_State* L, int i, float d) { return static_cast<float>(luaL_optnumber(L, i, d)); }

    /// Optional int at stack index i, defaulting to d when absent/nil.
    inline int OptInt(lua_State* L, int i, int d) { return static_cast<int>(luaL_optinteger(L, i, d)); }

    /// Optional bool at stack index i (any truthy Lua value), defaulting to d when absent/nil.
    inline bool OptBool(lua_State* L, int i, bool d) { return lua_isnoneornil(L, i) ? d : (lua_toboolean(L, i) != 0); }

    /// Optional flag mask at stack index i, defaulting to 0 (ImGui's "no flags").
    inline int OptFlags(lua_State* L, int i) { return static_cast<int>(luaL_optinteger(L, i, 0)); }

    /// Pushes an ImVec2 as two numbers (x, y) and reports the pushed count for a direct `return`.
    inline int PushVec2(lua_State* L, const ImVec2& v) { lua_pushnumber(L, v.x); lua_pushnumber(L, v.y); return 2; }

    // --- color marshalling ---------------------------------------------------------------------------

    /// Reads four consecutive stack slots (r, g, b at i..i+2 required, a at i+3 optional) as an ImVec4.
    /// High-level widgets take colors as floats in 0..1; alpha defaults to da.
    inline ImVec4 ReadColor4(lua_State* L, int i, float da = 1.0f)
    {
        const float r = static_cast<float>(luaL_checknumber(L, i));
        const float g = static_cast<float>(luaL_checknumber(L, i + 1));
        const float b = static_cast<float>(luaL_checknumber(L, i + 2));
        const float a = static_cast<float>(luaL_optnumber(L, i + 3, da));
        return ImVec4(r, g, b, a);
    }

    /// Reads a packed ImU32 color (as produced by wxl.ui.color) from stack index i. Carried as a Lua
    /// number because a 32-bit color overflows lua_Integer on the 32-bit client build.
    inline ImU32 ReadU32(lua_State* L, int i) { return static_cast<ImU32>(static_cast<unsigned>(luaL_checknumber(L, i))); }

    /// wxl.ui.color(r, g, b, a=1) -> packed: folds four 0..1 floats into a 32-bit ImU32 color, the form
    /// the draw-list and *_packed style helpers expect. Returned as a Lua number to survive the 32-bit build.
    inline int L_color(lua_State* L)
    {
        const float r = static_cast<float>(luaL_checknumber(L, 1));
        const float g = static_cast<float>(luaL_checknumber(L, 2));
        const float b = static_cast<float>(luaL_checknumber(L, 3));
        const float a = static_cast<float>(luaL_optnumber(L, 4, 1.0));
        const ImU32 c = ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, a));
        lua_pushnumber(L, static_cast<lua_Number>(static_cast<unsigned>(c)));
        return 1;
    }

    // --- ImDrawList handle ---------------------------------------------------------------------------

    struct DrawListHandle
    {
        ImDrawList* list;
        uint64_t generation;
    };

    /// Wraps a raw ImDrawList* as a Lua full userdata carrying the kDrawListMeta metatable. The pointer is
    /// owned by ImGui and valid only within the current frame, so handles must not be stashed across frames.
    inline void PushDrawList(lua_State* L, ImDrawList* dl)
    {
        auto* ud = static_cast<DrawListHandle*>(lua_newuserdata(L, sizeof(DrawListHandle)));
        ud->list = dl;
        ud->generation = wxl::lua::ui::FrameGeneration();
        luaL_getmetatable(L, kDrawListMeta);
        lua_setmetatable(L, -2);
    }

    /// Retrieves the ImDrawList* from a draw-list handle at stack index i, raising a Lua type error if the
    /// argument is not one. Also re-checks the frame is open, since the pointer would otherwise dangle.
    inline ImDrawList* CheckDrawList(lua_State* L, int i, const char* fn)
    {
        auto* ud = static_cast<DrawListHandle*>(luaL_checkudata(L, i, kDrawListMeta));
        if (!wxl::lua::ui::InFrame())
            luaL_error(L, "wxl.ui.drawlist:%s: draw-list handles are only valid inside the 'draw' handler that produced them", fn);
        if (ud->generation != wxl::lua::ui::FrameGeneration())
            luaL_error(L, "wxl.ui.drawlist:%s: this draw-list handle belongs to an earlier frame", fn);
        return ud->list;
    }

    /// Creates the empty kDrawListMeta metatable (with __index pointing at itself) so the DrawList methods
    /// can be attached to it later. Stack-neutral; call once before RegisterDrawList.
    inline void RegisterDrawListMeta(lua_State* L)
    {
        luaL_newmetatable(L, kDrawListMeta);
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "__index"); // methods resolve on the metatable itself
        lua_pop(L, 1);
    }

    /// Adds the shared wxl.ui.* helpers (currently `color`) to the ui subtable on top of the stack.
    inline void RegisterCommon(lua_State* L)
    {
        lua_pushcfunction(L, &L_color); lua_setfield(L, -2, "color");
    }
}
