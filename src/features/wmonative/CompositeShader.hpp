// Modern-WMO Composite fix: composite the second layer by the texture's own alpha, in-memory PS patch.
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

#include <cstdint>

/**
 * @brief Read-only query surface of the modern-WMO Composite pixel-shader fix (features/wmonative).
 *
 * A modern WMO material's second texture is an alpha-masked DETAIL OVERLAY (edge highlights, panel
 * lines): measured 62-92% transparent across the corpus, meant to be composited over the base layer by
 * its OWN alpha. 3.3.5's two-layer effect (MapObjComposite, effect id 6) blends the two layers by the
 * SECONDARY VERTEX-COLOUR alpha instead (`lrp r2, v1.w, r0, r1`), ignoring the texture alpha -- so it
 * mixes in the overlay's dark body and its highlight lines render as dark stripes.
 *
 * The fix patches the stock Composite pixel shader IN MEMORY -- exactly like features/adtsplit height
 * blend: the wrapper's bytecode is read out, D3DDisassemble'd, the one blend instruction is rewritten to
 * `lrp <d>, <tex2>.w, <tex2>, <tex1>` = lerp(tex1, tex2, tex2.a), reassembled, and swapped into GxState
 * slot 0x4E for the draw. No .bls file is produced or replaced. The swap is scoped to MODERN WMOs (stock
 * two-layer WMOs still want the vertex-alpha blend): two thin frame-stash hooks on the WMO batch-draw
 * leaves record the current root's modern verdict, and a post-hook on the effect bind performs the swap
 * only when that verdict holds and the active effect collection is the Composite one.
 */
namespace wxl::runtime::wmocomposite
{
    struct Stats
    {
        uint32_t patchedShaders; ///< distinct stock Composite PS permutations successfully patched
        uint32_t patchFailures;  ///< permutations whose disassembly did not match / failed to reassemble
        uint32_t batchesSwapped; ///< modern Composite batches drawn with the patched shader (cumulative)
        uint32_t vsPatched;      ///< distinct single-layer VS permutations re-sourced onto UV set 1
        uint32_t vsSwapped;      ///< single-layer batches drawn with the set1 VS (cumulative, per frame)
    };

    Stats GetStats();

    /** @brief True when the feature is compiled in (config.hpp kWmoCompositeAlphaFix). */
    bool Enabled();

    /** @brief True once both leaf hooks and the effect-bind hook installed. */
    bool Installed();

    /**
     * @brief Live A/B switch. True (default) composites the second layer by the texture alpha; false
     *        leaves the stock secondary-vertex-alpha blend, so the dark stripes can be seen come back.
     */
    bool FixEnabled();
    void SetFixEnabled(bool on);

    /**
     * @brief EXPERIMENTAL: which vertex UV set the base texture samples in the patched Composite shader.
     *        0 = set 0 (stock routing); 1 = set 1 (the base is re-sourced from the second texcoord the VS
     *        already emits). Modern shaders route each texture to a specific UV set; this tests, live, the
     *        "base wants set 1" hypothesis for the wrong-orientation textures. Takes effect on the next
     *        batch (the patched shader is cached per set).
     */
    int  BaseUvSet();
    void SetBaseUvSet(int set);

    /**
     * @brief Live A/B of the single-layer set1 vertex-shader route. True (default) binds, per single-layer
     *        batch of a modern two-UV group, a VS copy that samples UV set 1 -- so the base texture lands on
     *        the group's LAST UV set (Legion's rule) with no change to the shared vertex buffer. False leaves
     *        the stock VS (single-layer base on set 0), for A/B.
     */
    bool VsRouteEnabled();
    void SetVsRouteEnabled(bool on);
}
