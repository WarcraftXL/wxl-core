// Compacts the source particle emitter and scopes the source alpha-key cutoff at draw time.
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

// The byte-transform half (Compact) compiles into both the DLL and the host; the draw half
// (OnSetupBatchAlpha) is live-engine and DLL-only, so it and its event dependency are excluded from
// the host build (WXL_HOST).
#ifndef WXL_HOST
#include "engine/events/Event.hpp"
#endif

/**
 * @brief Compacts source particle emitters onto the client stride and scopes the alpha-key cutoff at draw.
 *
 * The source particle emitter is wider than the client's and carries source-only encodings (packed
 * multi-texture id, BlendAdd mode, wrapping flipbook cells, compressed gravity). This theme slides
 * each emitter onto the client stride and normalizes those encodings at load, and lowers the
 * alpha-key cutoff for source content at draw.
 */
namespace wxl::modern::assets::m2::particles
{
    /// Blend mode 1 = alpha key — the only mode the lowered source cutoff applies to. Public so
    /// draw-frequency callers can test it BEFORE paying any per-batch lookup.
    inline constexpr uint16_t kBlendAlphaKey = 1;

    /**
     * @brief Compacts every source emitter onto the client stride in place and normalizes its
     *        source-only encodings.
     * @param md        Model header (pre-parse, offsets model-relative).
     * @param fileSize  Total model size, bounding the sub-array reads.
     */
    void Compact(wxl::structure::m2::M2Header* md, uint32_t fileSize);

#ifndef WXL_HOST
    /**
     * @brief Lowers the alpha-key cutoff to the source coverage midpoint for an alpha-key batch of a
     *        downported model.
     *
     * Native content (downported = false) keeps its vanilla cutoff.
     * @param a           Batch alpha/material setup arguments.
     * @param downported  True if the model was reshaped by this module.
     */
    void OnSetupBatchAlpha(const wxl::events::M2SetupBatchAlphaArgs& a, bool downported);
#endif
}
