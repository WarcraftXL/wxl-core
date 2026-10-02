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

#include "Particles.hpp"

#include "wxl/game/M2.hpp" // PushAlphaRef: live-engine, DLL-only draw path

namespace wxl::modern::assets::m2::particles
{
    namespace
    {
        // Where source leaf / foliage coverage alpha sits; blend mode 1 (alpha key) is the only mode
        // the lowered reference applies to.
        constexpr float kSourceAlphaKeyRef = 0.5f;
    }

    /**
     * @brief Lowers the alpha-key cutoff to the source coverage midpoint for an alpha-key batch of a
     *        downported model.
     * @param a           Batch alpha/material setup arguments.
     * @param downported  True if the model was reshaped by this module.
     */
    void OnSetupBatchAlpha(const wxl::events::M2SetupBatchAlphaArgs& a, bool downported)
    {
        if (downported && a.blendMode == kBlendAlphaKey)
            wxl::game::m2::PushAlphaRef(kSourceAlphaKeyRef);
    }
}
