// Compacts the source camera record onto the client camera stride.
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

#include "engine/assets/m2/M2Format.hpp"

/**
 * @brief Compacts source cameras onto the client camera stride.
 *
 * The source camera drops the explicit fov float and appends a FoV animation track, so it is 16
 * bytes larger than the client camera. The track body is identical, so each record compacts in place.
 */
namespace wxl::modern::assets::m2::cameras
{
    /**
     * @brief Compacts every source camera onto the client camera stride in place.
     * @param md  Model header (pre-parse, offsets model-relative).
     */
    void Compact(wxl::structure::m2::M2Header* md);
}
