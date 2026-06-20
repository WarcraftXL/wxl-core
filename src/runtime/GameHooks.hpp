// Game-logic detours that publish the non-render events (model load, doodad spawn, world lifecycle...).
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

namespace wxl::runtime::game
{
    /**
     * @brief Installs the function-entry detours that republish game-logic events.
     *
     * Emits OnModelLoad, OnDoodadSpawn, OnWorldEnter, OnWorldLeave, OnTextureUpload and
     * OnAdtChunkBuild. The caller runs hook::EnableAll() once after every installer.
     */
    void Install();
}
