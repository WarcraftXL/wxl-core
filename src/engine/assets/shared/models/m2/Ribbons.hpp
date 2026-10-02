// Opts a multi-layer ribbon into the single-pass texture combine at draw.
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

#include "wxl/Events.hpp"

/**
 * @brief Opts a multi-layer ribbon into the single-pass texture combine at draw.
 *
 * A >= 3 layer ribbon cannot be reproduced by drawing one single-texture pass per layer, so those
 * ribbons take the combine instead. The draw-time hook fires for every ribbon regardless of which
 * pipeline produced the model, so only the layer count decides, not model origin.
 */
namespace wxl::modern::assets::m2::ribbons
{
    /**
     * @brief Requests the single-pass multi-texture combine at a ribbon draw.
     *
     * The core applies it for >= 3 layers.
     * @param a  Ribbon draw arguments.
     */
    void OnRibbonDraw(const wxl::events::RibbonDrawArgs& a);
}
