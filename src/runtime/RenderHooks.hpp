// Render-pipeline detours that publish the render events.
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

namespace wxl::runtime::render
{
    /**
     * @brief Installs the render-pipeline detours that republish render events.
     *
     * Emits OnM2BatchDraw, OnEndScene, OnFrame and OnWorldRenderEnd. Call once after the graphics
     * device is up; the function detours are armed by the caller's EnableAll() afterwards.
     */
    void Install();
}
