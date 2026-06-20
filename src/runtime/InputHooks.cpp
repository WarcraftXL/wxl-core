// Window-input detour: subclass the client window and publish OnInput, swallowing consumed messages.
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

#include "runtime/InputHooks.hpp"

#include "core/Logger.hpp"
#include "events/Event.hpp"

#include <windows.h>

namespace
{
    namespace ev = wxl::events;

    HWND    g_hwnd        = nullptr;
    WNDPROC g_origWndProc = nullptr;

    /**
     * @brief Selects the top-level visible window owned by this process.
     * @param h    candidate window handle from the enumeration.
     * @param out  receives the matched HWND.
     * @return FALSE to stop enumeration on a match, TRUE to continue.
     */
    BOOL CALLBACK PickWindow(HWND h, LPARAM out)
    {
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        if (pid == GetCurrentProcessId() && GetWindow(h, GW_OWNER) == nullptr && IsWindowVisible(h))
        {
            *reinterpret_cast<HWND*>(out) = h;
            return FALSE;
        }
        return TRUE;
    }

    /**
     * @brief Finds the client window by enumeration, with a window-class lookup fallback.
     * @return the window handle, or null if none is found.
     */
    HWND FindGameWindow()
    {
        HWND h = nullptr;
        EnumWindows(&PickWindow, reinterpret_cast<LPARAM>(&h));
        if (!h) h = FindWindowA("GxWindowClass", nullptr);
        return h;
    }

    /**
     * @brief Republishes every window message as OnInput, swallowing it when a subscriber sets handled.
     * @param h  window handle.
     * @param m  message id.
     * @param w  message WPARAM.
     * @param l  message LPARAM.
     * @return 0 when the message is consumed, otherwise the original window procedure result.
     */
    LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l)
    {
        bool handled = false;
        ev::InputArgs a{ m, static_cast<uintptr_t>(w), static_cast<uintptr_t>(l), &handled };
        ev::Emit(ev::Event::OnInput, &a);
        if (handled) return 0;
        return CallWindowProcA(g_origWndProc, h, m, w, l);
    }
}

namespace wxl::runtime::input
{
    /**
     * @brief Subclasses the client window and routes its messages through WndProc.
     */
    void Install()
    {
        if (g_origWndProc) return; // already installed
        g_hwnd = FindGameWindow();
        if (!g_hwnd) { WLOG_WARN("input: game window not found, OnInput inactive"); return; }

        g_origWndProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrA(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WndProc)));
        if (!g_origWndProc) { WLOG_WARN("input: SetWindowLongPtr failed (%lu)", GetLastError()); return; }

        WLOG_INFO("input: window subclassed (hwnd=%p), OnInput live", g_hwnd);
    }
}
