// Scopes the source alpha-key cutoff at draw time.
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
#include "wxl/formats/M2Format.hpp"

/**
 * @brief Scopes the alpha-key cutoff a source-authored batch expects, at draw time.
 *
 * Source content authors coverage alpha against a lower cutoff than the target's default, so an
 * alpha-key batch of a reshaped model needs its own reference pushed for the duration of that batch.
 * It is device state, not a record field, which is why it lives at draw and not at load.
 */
namespace wxl::modern::assets::m2::particles
{
    /// Blend mode 1 = alpha key -- the only mode the lowered source cutoff applies to. Public so
    /// draw-frequency callers can test it BEFORE paying any per-batch lookup.
    inline constexpr uint16_t kBlendAlphaKey = 1;

    /**
     * @brief Lowers the alpha-key cutoff to the source coverage midpoint for an alpha-key batch of a
     *        downported model.
     *
     * Native content (downported = false) keeps its vanilla cutoff.
     * @param a           Batch alpha/material setup arguments.
     * @param downported  True if the model was reshaped by this module.
     */
    void OnSetupBatchAlpha(const wxl::events::M2SetupBatchAlphaArgs& a, bool downported);
}
