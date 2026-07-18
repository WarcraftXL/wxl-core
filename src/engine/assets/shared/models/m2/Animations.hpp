// Masks the source blend time and remaps out-of-range sequence ids onto client ids.
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
 * @brief Masks the source blend time and remaps out-of-range sequence ids onto client ids.
 *
 * The source splits each sequence's blend time into in|out halves and ships a few sequence ids
 * above what the client engine resolves. This theme masks the blend time back to a single value and
 * remaps a curated id set to a client id, patching the sequence lookup so the engine still resolves them.
 */
namespace wxl::modern::assets::m2::animations
{
    /**
     * @brief Fixes every sequence in place.
     * @param md  Model header (pre-parse, offsets model-relative).
     */
    void Fix(wxl::structure::m2::M2Header* md);
}
