// UI drags & sliders: float/int drag and slider widgets (1..4 components) plus slider_angle for wxl.ui.*.
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

/// Drag and slider widgets. Each returns its new value(s) followed by a trailing `changed` boolean.
/// Multi-component variants take/return N consecutive numbers.
namespace wxl::lua::methods::ui
{
    /// Reads n consecutive numbers starting at stack index base into out.
    inline void ReadFloats(lua_State* L, int base, float* out, int n)
    {
        for (int k = 0; k < n; ++k)
            out[k] = static_cast<float>(luaL_checknumber(L, base + k));
    }

    /// Reads n consecutive integers starting at stack index base into out.
    inline void ReadInts(lua_State* L, int base, int* out, int n)
    {
        for (int k = 0; k < n; ++k)
            out[k] = static_cast<int>(luaL_checkinteger(L, base + k));
    }

    /// Pushes n floats followed by the changed flag; returns the pushed count for a direct `return`.
    inline int PushFloats(lua_State* L, const float* v, int n, bool changed)
    {
        for (int k = 0; k < n; ++k) lua_pushnumber(L, v[k]);
        lua_pushboolean(L, changed);
        return n + 1;
    }

    /// Pushes n ints followed by the changed flag; returns the pushed count for a direct `return`.
    inline int PushInts(lua_State* L, const int* v, int n, bool changed)
    {
        for (int k = 0; k < n; ++k) lua_pushinteger(L, v[k]);
        lua_pushboolean(L, changed);
        return n + 1;
    }

    // --- drag float N: drag_floatK(label, v1..vK[, speed[, min[, max]]]) -> v1..vK, changed -----------

    template <int N>
    inline int DragFloatN(lua_State* L, const char* fn)
    {
        if (!InDraw(L, fn)) return 0;
        const char* label = luaL_checkstring(L, 1);
        float       v[N];
        ReadFloats(L, 2, v, N);
        const float speed = OptFloat(L, 2 + N, 1.0f);
        const float mn    = OptFloat(L, 3 + N, 0.0f);
        const float mx    = OptFloat(L, 4 + N, 0.0f);
        bool        c;
        if constexpr (N == 1) c = ImGui::DragFloat(label, v, speed, mn, mx);
        else if constexpr (N == 2) c = ImGui::DragFloat2(label, v, speed, mn, mx);
        else if constexpr (N == 3) c = ImGui::DragFloat3(label, v, speed, mn, mx);
        else c = ImGui::DragFloat4(label, v, speed, mn, mx);
        return PushFloats(L, v, N, c);
    }

    /// wxl.ui.drag_float(label, v[, speed[, min[, max]]]) -> v, changed.
    inline int L_dragFloat(lua_State* L)  { return DragFloatN<1>(L, "drag_float"); }
    /// wxl.ui.drag_float2(label, x, y[, speed[, min[, max]]]) -> x, y, changed.
    inline int L_dragFloat2(lua_State* L) { return DragFloatN<2>(L, "drag_float2"); }
    /// wxl.ui.drag_float3(label, x, y, z[, speed[, min[, max]]]) -> x, y, z, changed.
    inline int L_dragFloat3(lua_State* L) { return DragFloatN<3>(L, "drag_float3"); }
    /// wxl.ui.drag_float4(label, x, y, z, w[, speed[, min[, max]]]) -> x, y, z, w, changed.
    inline int L_dragFloat4(lua_State* L) { return DragFloatN<4>(L, "drag_float4"); }

    // --- drag int N: drag_intK(label, v1..vK[, speed[, min[, max]]]) -> v1..vK, changed ---------------

    template <int N>
    inline int DragIntN(lua_State* L, const char* fn)
    {
        if (!InDraw(L, fn)) return 0;
        const char* label = luaL_checkstring(L, 1);
        int         v[N];
        ReadInts(L, 2, v, N);
        const float speed = OptFloat(L, 2 + N, 1.0f);
        const int   mn    = OptInt(L, 3 + N, 0);
        const int   mx    = OptInt(L, 4 + N, 0);
        bool        c;
        if constexpr (N == 1) c = ImGui::DragInt(label, v, speed, mn, mx);
        else if constexpr (N == 2) c = ImGui::DragInt2(label, v, speed, mn, mx);
        else if constexpr (N == 3) c = ImGui::DragInt3(label, v, speed, mn, mx);
        else c = ImGui::DragInt4(label, v, speed, mn, mx);
        return PushInts(L, v, N, c);
    }

    /// wxl.ui.drag_int(label, v[, speed[, min[, max]]]) -> v, changed.
    inline int L_dragInt(lua_State* L)  { return DragIntN<1>(L, "drag_int"); }
    /// wxl.ui.drag_int2(label, x, y[, speed[, min[, max]]]) -> x, y, changed.
    inline int L_dragInt2(lua_State* L) { return DragIntN<2>(L, "drag_int2"); }
    /// wxl.ui.drag_int3(label, x, y, z[, speed[, min[, max]]]) -> x, y, z, changed.
    inline int L_dragInt3(lua_State* L) { return DragIntN<3>(L, "drag_int3"); }
    /// wxl.ui.drag_int4(label, x, y, z, w[, speed[, min[, max]]]) -> x, y, z, w, changed.
    inline int L_dragInt4(lua_State* L) { return DragIntN<4>(L, "drag_int4"); }

    // --- slider float N: slider_floatK(label, v1..vK, min, max) -> v1..vK, changed --------------------

    template <int N>
    inline int SliderFloatN(lua_State* L, const char* fn)
    {
        if (!InDraw(L, fn)) return 0;
        const char* label = luaL_checkstring(L, 1);
        float       v[N];
        ReadFloats(L, 2, v, N);
        const float mn = static_cast<float>(luaL_checknumber(L, 2 + N));
        const float mx = static_cast<float>(luaL_checknumber(L, 3 + N));
        bool        c;
        if constexpr (N == 1) c = ImGui::SliderFloat(label, v, mn, mx);
        else if constexpr (N == 2) c = ImGui::SliderFloat2(label, v, mn, mx);
        else if constexpr (N == 3) c = ImGui::SliderFloat3(label, v, mn, mx);
        else c = ImGui::SliderFloat4(label, v, mn, mx);
        return PushFloats(L, v, N, c);
    }

    /// wxl.ui.slider_float(label, v, min, max) -> v, changed. Also exported as wxl.ui.slider.
    inline int L_sliderFloat(lua_State* L)  { return SliderFloatN<1>(L, "slider_float"); }
    /// wxl.ui.slider_float2(label, x, y, min, max) -> x, y, changed.
    inline int L_sliderFloat2(lua_State* L) { return SliderFloatN<2>(L, "slider_float2"); }
    /// wxl.ui.slider_float3(label, x, y, z, min, max) -> x, y, z, changed.
    inline int L_sliderFloat3(lua_State* L) { return SliderFloatN<3>(L, "slider_float3"); }
    /// wxl.ui.slider_float4(label, x, y, z, w, min, max) -> x, y, z, w, changed.
    inline int L_sliderFloat4(lua_State* L) { return SliderFloatN<4>(L, "slider_float4"); }

    // --- slider int N: slider_intK(label, v1..vK, min, max) -> v1..vK, changed ------------------------

    template <int N>
    inline int SliderIntN(lua_State* L, const char* fn)
    {
        if (!InDraw(L, fn)) return 0;
        const char* label = luaL_checkstring(L, 1);
        int         v[N];
        ReadInts(L, 2, v, N);
        const int   mn = static_cast<int>(luaL_checkinteger(L, 2 + N));
        const int   mx = static_cast<int>(luaL_checkinteger(L, 3 + N));
        bool        c;
        if constexpr (N == 1) c = ImGui::SliderInt(label, v, mn, mx);
        else if constexpr (N == 2) c = ImGui::SliderInt2(label, v, mn, mx);
        else if constexpr (N == 3) c = ImGui::SliderInt3(label, v, mn, mx);
        else c = ImGui::SliderInt4(label, v, mn, mx);
        return PushInts(L, v, N, c);
    }

    /// wxl.ui.slider_int(label, v, min, max) -> v, changed.
    inline int L_sliderInt(lua_State* L)  { return SliderIntN<1>(L, "slider_int"); }
    /// wxl.ui.slider_int2(label, x, y, min, max) -> x, y, changed.
    inline int L_sliderInt2(lua_State* L) { return SliderIntN<2>(L, "slider_int2"); }
    /// wxl.ui.slider_int3(label, x, y, z, min, max) -> x, y, z, changed.
    inline int L_sliderInt3(lua_State* L) { return SliderIntN<3>(L, "slider_int3"); }
    /// wxl.ui.slider_int4(label, x, y, z, w, min, max) -> x, y, z, w, changed.
    inline int L_sliderInt4(lua_State* L) { return SliderIntN<4>(L, "slider_int4"); }

    /// wxl.ui.slider_angle(label, radians[, deg_min[, deg_max]]) -> radians, changed: a slider that edits
    /// an angle stored in radians while displaying degrees.
    inline int L_sliderAngle(lua_State* L)
    {
        if (!InDraw(L, "slider_angle")) return 0;
        const char* label = luaL_checkstring(L, 1);
        float       rad   = static_cast<float>(luaL_checknumber(L, 2));
        const float dmin  = OptFloat(L, 3, -360.0f);
        const float dmax  = OptFloat(L, 4, +360.0f);
        const bool  c     = ImGui::SliderAngle(label, &rad, dmin, dmax);
        lua_pushnumber(L, rad);
        lua_pushboolean(L, c);
        return 2;
    }

    /// Adds the drag/slider fields to the ui subtable on top of the stack. `slider` aliases `slider_float`
    /// to preserve the original wxl.ui.slider(label, v, min, max) surface.
    inline void RegisterDrags(lua_State* L)
    {
        static const luaL_Reg fns[] = {
            { "drag_float",   L_dragFloat },
            { "drag_float2",  L_dragFloat2 },
            { "drag_float3",  L_dragFloat3 },
            { "drag_float4",  L_dragFloat4 },
            { "drag_int",     L_dragInt },
            { "drag_int2",    L_dragInt2 },
            { "drag_int3",    L_dragInt3 },
            { "drag_int4",    L_dragInt4 },
            { "slider_float", L_sliderFloat },
            { "slider",       L_sliderFloat }, // preserved MVP alias
            { "slider_float2",L_sliderFloat2 },
            { "slider_float3",L_sliderFloat3 },
            { "slider_float4",L_sliderFloat4 },
            { "slider_int",   L_sliderInt },
            { "slider_int2",  L_sliderInt2 },
            { "slider_int3",  L_sliderInt3 },
            { "slider_int4",  L_sliderInt4 },
            { "slider_angle", L_sliderAngle },
            { nullptr, nullptr },
        };
        SetFunctions(L, fns);
    }
}
