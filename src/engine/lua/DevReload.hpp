// Dev-mode hot reload: polls the extensions folder and signals a full VM reload on change.
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

/// Optional dev convenience (WXL_DEV_MODE truthy): watch the extensions folder and ask for a
/// reload when its contents change. Deliberately simple and predictable: a periodic poll of the
/// directory's names + timestamps + sizes, compared against a snapshot. No worker thread ever
/// touches the lua_State — PollChanged() is called from the game thread and the engine performs the
/// Stop/Start reload itself, so all Lua work stays single-threaded.
namespace wxl::lua::dev
{
    /** @brief True when dev hot-reload is enabled (WXL_DEV_MODE truthy). */
    bool Enabled();

    /** @brief Records the current folder state as the reload baseline. Called after each (re)start. */
    void Snapshot();

    /**
     * @brief Throttled poll (>= 500 ms between disk scans) for a change since the last baseline.
     *
     * On the first call it adopts the current state as the baseline and reports no change. When a
     * change is detected the baseline advances so the reload fires once per edit, not every frame.
     * @return true when the extensions folder changed and a reload is due.
     */
    bool PollChanged();
}
