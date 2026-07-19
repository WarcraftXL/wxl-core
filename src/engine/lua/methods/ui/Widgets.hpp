// UI widgets: buttons, checkbox, radio, progress bar and color widgets for wxl.ui.*.
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

#include <cfloat>

#include "engine/lua/Marshal.hpp"
#include "engine/lua/methods/ui/Common.hpp"
#include "engine/lua/methods/TextureMethods.hpp"

/// Interactive widgets. Action widgets (button family) return their pressed bool; value widgets
/// (checkbox, color edits) return the new value(s) followed by a trailing `changed` boolean.
namespace wxl::lua::methods::ui
{
    /// wxl.ui.button(label) -> clicked: draws a button, true on the frame it is pressed.
    inline int L_button(lua_State* L)
    {
        if (!InDraw(L, "button")) return 0;
        lua_pushboolean(L, ImGui::Button(luaL_checkstring(L, 1)));
        return 1;
    }

    /// wxl.ui.small_button(label) -> clicked: a compact button with no vertical frame padding.
    inline int L_smallButton(lua_State* L)
    {
        if (!InDraw(L, "small_button")) return 0;
        lua_pushboolean(L, ImGui::SmallButton(luaL_checkstring(L, 1)));
        return 1;
    }

    /// wxl.ui.invisible_button(id, w, h) -> clicked: an interaction area with no visuals.
    inline int L_invisibleButton(lua_State* L)
    {
        if (!InDraw(L, "invisible_button")) return 0;
        const char* id = luaL_checkstring(L, 1);
        const float w  = static_cast<float>(luaL_checknumber(L, 2));
        const float h  = static_cast<float>(luaL_checknumber(L, 3));
        lua_pushboolean(L, ImGui::InvisibleButton(id, ImVec2(w, h)));
        return 1;
    }

    /// wxl.ui.arrow_button(id, dir) -> clicked: a square button showing an arrow. dir is wxl.ui.DIR.*.
    inline int L_arrowButton(lua_State* L)
    {
        if (!InDraw(L, "arrow_button")) return 0;
        const char* id = luaL_checkstring(L, 1);
        lua_pushboolean(L, ImGui::ArrowButton(id, static_cast<ImGuiDir>(luaL_checkinteger(L, 2))));
        return 1;
    }

    /// wxl.ui.checkbox(label, state) -> newState, changed: a checkbox seeded from state. newState is the
    /// value after this frame's interaction; changed is true only on the frame it flips.
    inline int L_checkbox(lua_State* L)
    {
        if (!InDraw(L, "checkbox")) return 0;
        const char* label = luaL_checkstring(L, 1);
        bool        state = lua_toboolean(L, 2) != 0;
        const bool  changed = ImGui::Checkbox(label, &state);
        lua_pushboolean(L, state);
        lua_pushboolean(L, changed);
        return 2;
    }

    /// wxl.ui.radio_button(label, active) -> clicked: a radio button drawn filled when active is true.
    /// It does not store state; the script decides what to do with clicked.
    inline int L_radioButton(lua_State* L)
    {
        if (!InDraw(L, "radio_button")) return 0;
        const char* label = luaL_checkstring(L, 1);
        lua_pushboolean(L, ImGui::RadioButton(label, lua_toboolean(L, 2) != 0));
        return 1;
    }

    /// wxl.ui.progress_bar(fraction[, w, h[, overlay]]): a filled bar for fraction in 0..1. w/h default to
    /// full-width auto-height; overlay is optional centered text.
    inline int L_progressBar(lua_State* L)
    {
        if (!InDraw(L, "progress_bar")) return 0;
        const float fraction = static_cast<float>(luaL_checknumber(L, 1));
        const ImVec2 size(OptFloat(L, 2, -FLT_MIN), OptFloat(L, 3, 0.0f));
        const char*  overlay = luaL_optstring(L, 4, nullptr);
        ImGui::ProgressBar(fraction, size, overlay);
        return 0;
    }

    /// wxl.ui.color_edit3(label, r, g, b[, flags]) -> r, g, b, changed: an RGB color editor (floats 0..1).
    inline int L_colorEdit3(lua_State* L)
    {
        if (!InDraw(L, "color_edit3")) return 0;
        const char* label = luaL_checkstring(L, 1);
        float col[3] = { static_cast<float>(luaL_checknumber(L, 2)),
                         static_cast<float>(luaL_checknumber(L, 3)),
                         static_cast<float>(luaL_checknumber(L, 4)) };
        const bool changed = ImGui::ColorEdit3(label, col, OptFlags(L, 5));
        lua_pushnumber(L, col[0]);
        lua_pushnumber(L, col[1]);
        lua_pushnumber(L, col[2]);
        lua_pushboolean(L, changed);
        return 4;
    }

    /// wxl.ui.color_edit4(label, r, g, b, a[, flags]) -> r, g, b, a, changed: an RGBA color editor.
    inline int L_colorEdit4(lua_State* L)
    {
        if (!InDraw(L, "color_edit4")) return 0;
        const char* label = luaL_checkstring(L, 1);
        float col[4] = { static_cast<float>(luaL_checknumber(L, 2)),
                         static_cast<float>(luaL_checknumber(L, 3)),
                         static_cast<float>(luaL_checknumber(L, 4)),
                         static_cast<float>(luaL_checknumber(L, 5)) };
        const bool changed = ImGui::ColorEdit4(label, col, OptFlags(L, 6));
        lua_pushnumber(L, col[0]);
        lua_pushnumber(L, col[1]);
        lua_pushnumber(L, col[2]);
        lua_pushnumber(L, col[3]);
        lua_pushboolean(L, changed);
        return 5;
    }

    /// wxl.ui.image(tex, w[, h]): draws a texture as an image widget in the current window. `tex` is a
    /// wxl.texture handle (preferred) or a numeric ImTextureID. h defaults to w when omitted (square).
    inline int L_image(lua_State* L)
    {
        if (!InDraw(L, "image")) return 0;
        const ImTextureID id = texture::CheckTextureId(L, 1);
        const float w = static_cast<float>(luaL_checknumber(L, 2));
        const float h = OptFloat(L, 3, w);
        ImGui::Image(ImTextureRef(id), ImVec2(w, h));
        return 0;
    }

    /// wxl.ui.color_button(id, r, g, b, a) -> clicked: a color swatch button (color as floats 0..1).
    inline int L_colorButton(lua_State* L)
    {
        if (!InDraw(L, "color_button")) return 0;
        const char*  id  = luaL_checkstring(L, 1);
        const ImVec4 col = ReadColor4(L, 2);
        lua_pushboolean(L, ImGui::ColorButton(id, col));
        return 1;
    }

    /// Adds the widget fields to the ui subtable on top of the stack.
    inline void RegisterWidgets(lua_State* L)
    {
        static const luaL_Reg fns[] = {
            { "button",           L_button },
            { "small_button",     L_smallButton },
            { "invisible_button", L_invisibleButton },
            { "arrow_button",     L_arrowButton },
            { "checkbox",         L_checkbox },
            { "radio_button",     L_radioButton },
            { "progress_bar",     L_progressBar },
            { "color_edit3",      L_colorEdit3 },
            { "color_edit4",      L_colorEdit4 },
            { "color_button",     L_colorButton },
            { "image",            L_image },
            { nullptr, nullptr },
        };
        SetFunctions(L, fns);
    }
}
