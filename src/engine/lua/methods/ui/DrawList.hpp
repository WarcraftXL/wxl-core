// UI draw list: raw primitive drawing over ImDrawList handles for wxl.ui.*.
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
#include "engine/lua/methods/TextureMethods.hpp"

/// Low-level drawing. window_draw_list()/foreground_draw_list()/background_draw_list() return a handle whose
/// methods draw primitives in absolute screen space. Colors are packed ImU32 values from wxl.ui.color(...).
/// Every method re-checks the frame is open, since a handle's pointer only lives for the frame that made it.
namespace wxl::lua::methods::ui
{
    /// wxl.ui.window_draw_list() -> handle: the current window's draw list (drawn with the window).
    inline int L_windowDrawList(lua_State* L)
    {
        if (!InDraw(L, "window_draw_list")) return 0;
        PushDrawList(L, ImGui::GetWindowDrawList());
        return 1;
    }

    /// wxl.ui.foreground_draw_list() -> handle: the shared draw list rendered above all windows.
    inline int L_foregroundDrawList(lua_State* L)
    {
        if (!InDraw(L, "foreground_draw_list")) return 0;
        PushDrawList(L, ImGui::GetForegroundDrawList());
        return 1;
    }

    /// wxl.ui.background_draw_list() -> handle: the shared draw list rendered behind all windows.
    inline int L_backgroundDrawList(lua_State* L)
    {
        if (!InDraw(L, "background_draw_list")) return 0;
        PushDrawList(L, ImGui::GetBackgroundDrawList());
        return 1;
    }

    /// handle:add_line(x1, y1, x2, y2, col[, thickness]).
    inline int L_dlAddLine(lua_State* L)
    {
        ImDrawList* dl = CheckDrawList(L, 1, "add_line");
        const float x1 = static_cast<float>(luaL_checknumber(L, 2));
        const float y1 = static_cast<float>(luaL_checknumber(L, 3));
        const float x2 = static_cast<float>(luaL_checknumber(L, 4));
        const float y2 = static_cast<float>(luaL_checknumber(L, 5));
        dl->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), ReadU32(L, 6), OptFloat(L, 7, 1.0f));
        return 0;
    }

    /// handle:add_rect(x1, y1, x2, y2, col[, rounding[, thickness]]).
    inline int L_dlAddRect(lua_State* L)
    {
        ImDrawList* dl = CheckDrawList(L, 1, "add_rect");
        const float x1 = static_cast<float>(luaL_checknumber(L, 2));
        const float y1 = static_cast<float>(luaL_checknumber(L, 3));
        const float x2 = static_cast<float>(luaL_checknumber(L, 4));
        const float y2 = static_cast<float>(luaL_checknumber(L, 5));
        dl->AddRect(ImVec2(x1, y1), ImVec2(x2, y2), ReadU32(L, 6), OptFloat(L, 7, 0.0f), OptFloat(L, 8, 1.0f), 0);
        return 0;
    }

    /// handle:add_rect_filled(x1, y1, x2, y2, col[, rounding]).
    inline int L_dlAddRectFilled(lua_State* L)
    {
        ImDrawList* dl = CheckDrawList(L, 1, "add_rect_filled");
        const float x1 = static_cast<float>(luaL_checknumber(L, 2));
        const float y1 = static_cast<float>(luaL_checknumber(L, 3));
        const float x2 = static_cast<float>(luaL_checknumber(L, 4));
        const float y2 = static_cast<float>(luaL_checknumber(L, 5));
        dl->AddRectFilled(ImVec2(x1, y1), ImVec2(x2, y2), ReadU32(L, 6), OptFloat(L, 7, 0.0f));
        return 0;
    }

    /// handle:add_circle(x, y, r, col[, segments[, thickness]]). segments of 0 auto-tessellates.
    inline int L_dlAddCircle(lua_State* L)
    {
        ImDrawList* dl = CheckDrawList(L, 1, "add_circle");
        const float x = static_cast<float>(luaL_checknumber(L, 2));
        const float y = static_cast<float>(luaL_checknumber(L, 3));
        const float r = static_cast<float>(luaL_checknumber(L, 4));
        dl->AddCircle(ImVec2(x, y), r, ReadU32(L, 5), OptInt(L, 6, 0), OptFloat(L, 7, 1.0f));
        return 0;
    }

    /// handle:add_circle_filled(x, y, r, col[, segments]). segments of 0 auto-tessellates.
    inline int L_dlAddCircleFilled(lua_State* L)
    {
        ImDrawList* dl = CheckDrawList(L, 1, "add_circle_filled");
        const float x = static_cast<float>(luaL_checknumber(L, 2));
        const float y = static_cast<float>(luaL_checknumber(L, 3));
        const float r = static_cast<float>(luaL_checknumber(L, 4));
        dl->AddCircleFilled(ImVec2(x, y), r, ReadU32(L, 5), OptInt(L, 6, 0));
        return 0;
    }

    /// handle:add_triangle_filled(x1, y1, x2, y2, x3, y3, col).
    inline int L_dlAddTriangleFilled(lua_State* L)
    {
        ImDrawList* dl = CheckDrawList(L, 1, "add_triangle_filled");
        const float x1 = static_cast<float>(luaL_checknumber(L, 2));
        const float y1 = static_cast<float>(luaL_checknumber(L, 3));
        const float x2 = static_cast<float>(luaL_checknumber(L, 4));
        const float y2 = static_cast<float>(luaL_checknumber(L, 5));
        const float x3 = static_cast<float>(luaL_checknumber(L, 6));
        const float y3 = static_cast<float>(luaL_checknumber(L, 7));
        dl->AddTriangleFilled(ImVec2(x1, y1), ImVec2(x2, y2), ImVec2(x3, y3), ReadU32(L, 8));
        return 0;
    }

    /// handle:add_text(x, y, col, s).
    inline int L_dlAddText(lua_State* L)
    {
        ImDrawList* dl = CheckDrawList(L, 1, "add_text");
        const float x = static_cast<float>(luaL_checknumber(L, 2));
        const float y = static_cast<float>(luaL_checknumber(L, 3));
        const ImU32 col = ReadU32(L, 4);
        dl->AddText(ImVec2(x, y), col, luaL_checkstring(L, 5));
        return 0;
    }

    /// handle:add_image(texture, x1, y1, x2, y2[, u1, v1, u2, v2[, col]]). `texture` is either a wxl.texture
    /// handle (preferred) or a raw numeric ImTextureID (e.g. from handle:id()); CheckTextureId resolves both.
    inline int L_dlAddImage(lua_State* L)
    {
        ImDrawList* dl = CheckDrawList(L, 1, "add_image");
        const ImTextureID tex = texture::CheckTextureId(L, 2);
        const float x1 = static_cast<float>(luaL_checknumber(L, 3));
        const float y1 = static_cast<float>(luaL_checknumber(L, 4));
        const float x2 = static_cast<float>(luaL_checknumber(L, 5));
        const float y2 = static_cast<float>(luaL_checknumber(L, 6));
        const ImVec2 uv0(OptFloat(L, 7, 0.0f), OptFloat(L, 8, 0.0f));
        const ImVec2 uv1(OptFloat(L, 9, 1.0f), OptFloat(L, 10, 1.0f));
        const ImU32  col = lua_isnoneornil(L, 11) ? IM_COL32_WHITE : ReadU32(L, 11);
        dl->AddImage(ImTextureRef(tex), ImVec2(x1, y1), ImVec2(x2, y2), uv0, uv1, col);
        return 0;
    }

    /// handle:push_clip_rect(x1, y1, x2, y2[, intersect]): clips subsequent draws to the rectangle.
    inline int L_dlPushClipRect(lua_State* L)
    {
        ImDrawList* dl = CheckDrawList(L, 1, "push_clip_rect");
        const float x1 = static_cast<float>(luaL_checknumber(L, 2));
        const float y1 = static_cast<float>(luaL_checknumber(L, 3));
        const float x2 = static_cast<float>(luaL_checknumber(L, 4));
        const float y2 = static_cast<float>(luaL_checknumber(L, 5));
        dl->PushClipRect(ImVec2(x1, y1), ImVec2(x2, y2), OptBool(L, 6, false));
        return 0;
    }

    /// handle:pop_clip_rect(): removes the last clip rectangle pushed via push_clip_rect.
    inline int L_dlPopClipRect(lua_State* L)
    {
        ImDrawList* dl = CheckDrawList(L, 1, "pop_clip_rect");
        dl->PopClipRect();
        return 0;
    }

    /// Adds the draw-list accessors to the ui subtable on top of the stack and attaches the handle methods
    /// to the kDrawListMeta metatable (which Common's RegisterDrawListMeta must have created first).
    inline void RegisterDrawList(lua_State* L)
    {
        static const luaL_Reg fns[] = {
            { "window_draw_list",     L_windowDrawList },
            { "foreground_draw_list", L_foregroundDrawList },
            { "background_draw_list", L_backgroundDrawList },
            { nullptr, nullptr },
        };
        SetFunctions(L, fns);

        luaL_getmetatable(L, kDrawListMeta); // handle methods live on the metatable (== its __index)
        static const luaL_Reg fns2[] = {
            { "add_line",            L_dlAddLine },
            { "add_rect",            L_dlAddRect },
            { "add_rect_filled",     L_dlAddRectFilled },
            { "add_circle",          L_dlAddCircle },
            { "add_circle_filled",   L_dlAddCircleFilled },
            { "add_triangle_filled", L_dlAddTriangleFilled },
            { "add_text",            L_dlAddText },
            { "add_image",           L_dlAddImage },
            { "push_clip_rect",      L_dlPushClipRect },
            { "pop_clip_rect",       L_dlPopClipRect },
            { nullptr, nullptr },
        };
        SetFunctions(L, fns2);
        lua_pop(L, 1); // pop metatable
    }
}
