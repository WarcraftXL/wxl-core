// Synthesizes the texture-coordinate-combos array a source model omits.
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

#include <string_view>

/**
 * @brief Synthesizes the texture-coordinate-combos array a source model omits.
 *
 * The client's skin finalize indexes textureCoordCombos (header.textureUnitLookup) unconditionally;
 * source creatures often ship it empty. This theme synthesizes a one-entry array so the parse-time
 * finalize does not index a null array (the in-memory rebuild later replaces it with the full contract).
 */
namespace wxl::modern::assets::m2::textures
{
    /**
     * @brief Reports whether the model omits the texture-coordinate-combos array.
     * @param md  Model header.
     * @return True if a one-entry synth is needed.
     */
    bool NeedsCoordCombos(const wxl::structure::m2::M2Header* md);

    /**
     * @brief Writes a one-entry texture-coordinate-combos array (set 0 = standard, no env) and points
     *        header.textureUnitLookup at it.
     * @param md            Model header. The caller must have reserved 2 bytes at appendOffset.
     * @param appendOffset  Model-relative offset where the entry is written.
     */
    void SynthCoordCombos(wxl::structure::m2::M2Header* md, uint32_t appendOffset);

    /**
     * @brief Computes extra bytes needed to append a repaired global-loop table for texture transforms.
     * @param md            Model header with file-relative offsets.
     * @param fileSize      Original model byte size.
     * @param appendOffset  Offset where prior grow steps would leave the cursor.
     * @return Extra bytes including alignment, or 0 when no loop repair is needed.
     */
    uint32_t TextureTransformLoopRepairExtraBytes(const wxl::structure::m2::M2Header* md,
                                                  uint32_t fileSize, uint32_t appendOffset,
                                                  std::string_view name = {});

    /**
     * @brief Rewrites one-shot/invalid texture-transform tracks to a global loop before native parse.
     * @param md            Model header with file-relative offsets.
     * @param fileSize      Original model byte size for bounds checks.
     * @param appendOffset  Offset where the repaired global-loop table may be written.
     * @param workSize      Total work-buffer size.
     * @return Count of repaired tracks.
     */
    uint32_t RepairTextureTransformLoops(wxl::structure::m2::M2Header* md, uint32_t fileSize,
                                         uint32_t appendOffset, uint32_t workSize,
                                         std::string_view name = {});

    struct WeaponBladeFixResult
    {
        uint32_t toObjectSkin = 0;
        uint32_t toHardcoded = 0;

        uint32_t Total() const { return toObjectSkin + toHardcoded; }
    };

    /**
     * @brief Rewrites source WEAPON_BLADE slots into a client-readable texture type.
     *
     * Some modern actor/item models incorrectly carry their primary skin texture as WEAPON_BLADE. If the slot
     * has no concrete filename, OBJECT_SKIN keeps the old replaceable-texture behavior. If a TXID/filename was
     * resolved, HARDCODED preserves that included texture instead of letting OBJECT_SKIN ignore it.
     * @param md        Model header.
     * @param fileSize  Model byte size for bounds checks.
     * @return Counts of texture records rewritten.
     */
    WeaponBladeFixResult FixWeaponBladeTextureTypes(wxl::structure::m2::M2Header* md, uint32_t fileSize);

    /**
     * @brief Reports whether `name` is a path family FixWeaponBladeTextureTypes should run on.
     *
     * Scoped to creature/character models and non-weapon item components: a weapon's own WEAPON_BLADE slot
     * is a legitimate replaceable-texture marker, not the misclassification this remap corrects.
     * @param name  Model path.
     * @return True if the remap applies to this path.
     */
    bool AllowsWeaponBladeRemap(std::string_view name);
}
