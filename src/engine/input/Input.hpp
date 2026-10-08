// Window-input detour: the one entry other core files call.
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

#include <windows.h>

namespace wxl::input
{
    /**
     * @brief Moves the subclass to @p hwnd when it is not the window OnInput listens on.
     *
     * Called by the backend-neutral frame hook only (no IDirect3DDevice9 in the process): a device
     * backend's format change destroys the client window and makes a new one, and the subclass went
     * with the old window. A no-op for the window already subclassed, for null, and before the input
     * feature installed.
     * @param hwnd  the engine device's current window.
     */
    void FollowWindow(HWND hwnd);
}
