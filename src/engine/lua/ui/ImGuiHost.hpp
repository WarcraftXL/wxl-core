// ImGui host: the Dear ImGui lifecycle (context, backends, per-frame pump), decoupled from Lua.
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

#include <cstdint>

/// The ImGui host owns Dear ImGui end to end and knows nothing about the wxl.ui surface: it creates the
/// context and the DX9/Win32 backends (lazily, on the first frame the device is up), pumps NewFrame ->
/// Render across the engine's render events, and forwards window input. It exposes the frame to Lua by
/// emitting the core bus event OnUiDraw once per frame, strictly between NewFrame and Render; the wxl.ui
/// methods bind to that window and refuse to run outside it. Everything here runs on the render/game
/// thread (the only thread that touches the device or the ImGui context), which the engine guarantees.
namespace wxl::lua::ui
{
    /**
     * @brief Subscribes the host to the render/input events. Called once at feature install, before any
     *        frame. Idempotent: a second call is a no-op. Does not create the ImGui context yet -- that
     *        happens lazily on the first OnFrame, once the D3D9 device exists.
     */
    void Install();

    /**
     * @brief Reports whether an ImGui frame is currently open for drawing, i.e. we are inside the OnUiDraw
     *        emission (between NewFrame and Render). The wxl.ui methods gate on this so a call from any
     *        other context (a "frame"/"update" handler, module scope) fails with a clear Lua error instead
     *        of tripping an ImGui assert.
     * @return true only while a 'draw' handler is running.
     */
    bool InFrame();

    /** @brief Monotonic id of the currently open draw frame; draw-list handles use it to reject reuse. */
    uint64_t FrameGeneration();
}
