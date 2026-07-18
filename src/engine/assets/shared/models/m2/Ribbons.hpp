// Clamps source ribbon indices at load and opts the multi-texture ribbon combine at draw.
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
// (OnRibbonDraw) is live-engine and DLL-only, so it and its event dependency are excluded from the
// host build (WXL_HOST).
#ifndef WXL_HOST
#include "engine/events/Event.hpp"
#endif

/**
 * @brief Clamps source ribbon indices at load and opts the multi-texture ribbon combine at draw.
 *
 * The source ribbon shares the client stride (its tail fields land in the client layout's padding),
 * so the load step only clamps its texture/material indices into the header tables. At draw, a >= 3
 * layer ribbon is opted into the single-pass multi-texture combine the engine's per-layer pass
 * cannot reproduce. The draw-time hook fires for every ribbon the client draws regardless of which
 * pipeline produced the model, so a future format whose ribbons legitimately reach 3+ layers gets
 * the combine for free; only the >= 3 layer count decides, not model origin.
 */
namespace wxl::modern::assets::m2::ribbons
{
    /**
     * @brief Clamps every source ribbon's texture/material indices into the header tables.
     * @param md        Model header (pre-parse, offsets model-relative).
     * @param fileSize  Total model size, bounding the sub-array reads.
     */
    void Compact(wxl::structure::m2::M2Header* md, uint32_t fileSize);

#ifndef WXL_HOST
    /**
     * @brief Requests the single-pass multi-texture combine at a ribbon draw.
     *
     * The core applies it for >= 3 layers.
     * @param a  Ribbon draw arguments.
     */
    void OnRibbonDraw(const wxl::events::RibbonDrawArgs& a);
#endif
}
