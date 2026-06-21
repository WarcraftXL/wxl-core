// Terrain-phase loader detour: redirects loaded ADT tiles to a phase-variant map directory.
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

namespace wxl::runtime::phasing
{
    /**
     * @brief Installs the per-tile ADT loader detour that applies the active terrain phase.
     *
     * With no active phase the detour is a pass-through. Set the phase via wxl::game::world::SetTerrainPhase.
     */
    void Install();
}
