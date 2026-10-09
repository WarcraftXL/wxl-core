// In-game ImGui host: device lifetime, input routing, and a registry of panels to draw.
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

#include <cstddef>

namespace wxl::ui
{
    /// A panel body. Called between NewFrame and Render, only while the overlay is open, so it may
    /// call ImGui freely and must not assume it runs every frame. The calling convention is spelled
    /// out because an extension's panel arrives through the plugin table, where it is fixed.
    using PanelFn = void (__cdecl *)(void* user);

    /**
     * @brief Registers a panel to be drawn whenever the overlay is open.
     *
     * Panels register at feature-install time, cold, and are never removed. The host owns no
     * knowledge of what any of them do -- a panel is the only thing a feature needs to write to put
     * controls on screen, and nothing about the device, the input routing or the reset dance is its
     * problem.
     *
     * @param title  window title, also its ImGui identity -- must be unique and stable.
     * @param fn     body, invoked inside an already-open window.
     * @param user   opaque pointer handed back to @p fn.
     */
    void AddPanel(const char* title, PanelFn fn, void* user);

    /// Show/hide a registered panel by title. A panel starts visible; its window's close button
    /// hides it too. Registry-only -- safe from WXL_Load, unlike the drawing calls.
    void SetPanelOpen(const char* title, bool open);
    bool IsPanelOpen(const char* title);

    /// Registered panels in registration order (title is null out of range). Registry-only.
    int PanelCount();
    const char* PanelTitle(int index);

    /// True while the overlay is open and taking input.
    bool IsOpen();

    // --- the controls an extension reaches through the plugin table ---
    // An extension cannot call the interface library: it would need the same build of it and a
    // compatible C++ ABI, which the plugin header exists to avoid requiring. These are the library
    // reduced to plain C calls the core makes on its behalf, and they are what WXL_Api::Ui* point at.
    // Each is meaningful only inside a panel body.
    namespace c
    {
        void __cdecl AddPanel(const char* title, void(__cdecl* fn)(void*), void* user);
        int  __cdecl IsOpen();
        void __cdecl SetPanelOpen(const char* title, int open);
        int  __cdecl IsPanelOpen(const char* title);
        int  __cdecl PanelCount();
        const char* __cdecl PanelTitle(int index);
        void __cdecl Text(const char* text);
        void __cdecl TextWrapped(const char* text);
        void __cdecl TextColored(const float rgba[4], const char* text);
        void __cdecl Separator();
        int  __cdecl Button(const char* label);
        int  __cdecl Checkbox(const char* label, int* value);
        int  __cdecl SliderFloat(const char* label, float* value, float min, float max);
        int  __cdecl SliderInt(const char* label, int* value, int min, int max);
        int  __cdecl ColorEdit(const char* label, float rgba[4]);
        void __cdecl SameLine();
        int  __cdecl Combo(const char* label, int* index, const char* const* items, int count);
        int  __cdecl CollapsingHeader(const char* label);
        int  __cdecl InputText(const char* label, char* buf, size_t bufSize);
    }
}
