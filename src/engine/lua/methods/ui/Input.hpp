// UI input fields: text and numeric input widgets for wxl.ui.*.
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

#include <cstring>
#include <vector>

#include "engine/lua/Marshal.hpp"
#include "engine/lua/methods/ui/Common.hpp"

/// Text and numeric input. Text widgets round-trip the current value through Lua every frame: a per-call
/// buffer is seeded from the incoming string, so there is no persistent C++ edit state to manage.
namespace wxl::lua::methods::ui
{
    /// wxl.ui.input_text(label, current[, max_len=256[, flags]]) -> text, changed: a single-line text box
    /// seeded from current. text is the buffer after this frame's edit; changed is true when it was edited.
    inline int L_inputText(lua_State* L)
    {
        if (!InDraw(L, "input_text")) return 0;
        const char*       label = luaL_checkstring(L, 1);
        const char*       cur   = luaL_checkstring(L, 2);
        int               cap   = OptInt(L, 3, 256);
        if (cap < 1) cap = 1;
        std::vector<char> buf(static_cast<size_t>(cap) + 1, '\0');
        std::strncpy(buf.data(), cur, buf.size() - 1);
        const bool changed = ImGui::InputText(label, buf.data(), buf.size(), OptFlags(L, 4));
        lua_pushstring(L, buf.data());
        lua_pushboolean(L, changed);
        return 2;
    }

    /// wxl.ui.input_text_multiline(label, current[, w[, h[, max_len=1024[, flags]]]]) -> text, changed:
    /// a multi-line text box. w/h of 0 auto-size.
    inline int L_inputTextMultiline(lua_State* L)
    {
        if (!InDraw(L, "input_text_multiline")) return 0;
        const char*       label = luaL_checkstring(L, 1);
        const char*       cur   = luaL_checkstring(L, 2);
        const ImVec2      size(OptFloat(L, 3, 0.0f), OptFloat(L, 4, 0.0f));
        int               cap   = OptInt(L, 5, 1024);
        if (cap < 1) cap = 1;
        std::vector<char> buf(static_cast<size_t>(cap) + 1, '\0');
        std::strncpy(buf.data(), cur, buf.size() - 1);
        const bool changed = ImGui::InputTextMultiline(label, buf.data(), buf.size(), size, OptFlags(L, 6));
        lua_pushstring(L, buf.data());
        lua_pushboolean(L, changed);
        return 2;
    }

    /// wxl.ui.input_float(label, v[, step[, step_fast[, flags]]]) -> v, changed.
    inline int L_inputFloat(lua_State* L)
    {
        if (!InDraw(L, "input_float")) return 0;
        const char* label = luaL_checkstring(L, 1);
        float       v     = static_cast<float>(luaL_checknumber(L, 2));
        const float step  = OptFloat(L, 3, 0.0f);
        const float fast  = OptFloat(L, 4, 0.0f);
        const bool  c     = ImGui::InputFloat(label, &v, step, fast, "%.3f", OptFlags(L, 5));
        lua_pushnumber(L, v);
        lua_pushboolean(L, c);
        return 2;
    }

    // --- input float N: input_floatK(label, v1..vK[, flags]) -> v1..vK, changed -----------------------

    template <int N>
    inline int InputFloatN(lua_State* L, const char* fn)
    {
        if (!InDraw(L, fn)) return 0;
        const char* label = luaL_checkstring(L, 1);
        float       v[N];
        for (int k = 0; k < N; ++k) v[k] = static_cast<float>(luaL_checknumber(L, 2 + k));
        const int   flags = OptFlags(L, 2 + N);
        bool        c;
        if constexpr (N == 2) c = ImGui::InputFloat2(label, v, "%.3f", flags);
        else if constexpr (N == 3) c = ImGui::InputFloat3(label, v, "%.3f", flags);
        else c = ImGui::InputFloat4(label, v, "%.3f", flags);
        for (int k = 0; k < N; ++k) lua_pushnumber(L, v[k]);
        lua_pushboolean(L, c);
        return N + 1;
    }

    /// wxl.ui.input_float2(label, x, y[, flags]) -> x, y, changed.
    inline int L_inputFloat2(lua_State* L) { return InputFloatN<2>(L, "input_float2"); }
    /// wxl.ui.input_float3(label, x, y, z[, flags]) -> x, y, z, changed.
    inline int L_inputFloat3(lua_State* L) { return InputFloatN<3>(L, "input_float3"); }
    /// wxl.ui.input_float4(label, x, y, z, w[, flags]) -> x, y, z, w, changed.
    inline int L_inputFloat4(lua_State* L) { return InputFloatN<4>(L, "input_float4"); }

    /// wxl.ui.input_int(label, v[, step[, step_fast[, flags]]]) -> v, changed.
    inline int L_inputInt(lua_State* L)
    {
        if (!InDraw(L, "input_int")) return 0;
        const char* label = luaL_checkstring(L, 1);
        int         v     = static_cast<int>(luaL_checkinteger(L, 2));
        const int   step  = OptInt(L, 3, 1);
        const int   fast  = OptInt(L, 4, 100);
        const bool  c     = ImGui::InputInt(label, &v, step, fast, OptFlags(L, 5));
        lua_pushinteger(L, v);
        lua_pushboolean(L, c);
        return 2;
    }

    // --- input int N: input_intK(label, v1..vK[, flags]) -> v1..vK, changed ---------------------------

    template <int N>
    inline int InputIntN(lua_State* L, const char* fn)
    {
        if (!InDraw(L, fn)) return 0;
        const char* label = luaL_checkstring(L, 1);
        int         v[N];
        for (int k = 0; k < N; ++k) v[k] = static_cast<int>(luaL_checkinteger(L, 2 + k));
        const int   flags = OptFlags(L, 2 + N);
        bool        c;
        if constexpr (N == 2) c = ImGui::InputInt2(label, v, flags);
        else if constexpr (N == 3) c = ImGui::InputInt3(label, v, flags);
        else c = ImGui::InputInt4(label, v, flags);
        for (int k = 0; k < N; ++k) lua_pushinteger(L, v[k]);
        lua_pushboolean(L, c);
        return N + 1;
    }

    /// wxl.ui.input_int2(label, x, y[, flags]) -> x, y, changed.
    inline int L_inputInt2(lua_State* L) { return InputIntN<2>(L, "input_int2"); }
    /// wxl.ui.input_int3(label, x, y, z[, flags]) -> x, y, z, changed.
    inline int L_inputInt3(lua_State* L) { return InputIntN<3>(L, "input_int3"); }
    /// wxl.ui.input_int4(label, x, y, z, w[, flags]) -> x, y, z, w, changed.
    inline int L_inputInt4(lua_State* L) { return InputIntN<4>(L, "input_int4"); }

    /// Adds the input fields to the ui subtable on top of the stack.
    inline void RegisterInput(lua_State* L)
    {
        static const luaL_Reg fns[] = {
            { "input_text",           L_inputText },
            { "input_text_multiline", L_inputTextMultiline },
            { "input_float",          L_inputFloat },
            { "input_float2",         L_inputFloat2 },
            { "input_float3",         L_inputFloat3 },
            { "input_float4",         L_inputFloat4 },
            { "input_int",            L_inputInt },
            { "input_int2",           L_inputInt2 },
            { "input_int3",           L_inputInt3 },
            { "input_int4",           L_inputInt4 },
            { nullptr, nullptr },
        };
        SetFunctions(L, fns);
    }
}
