// Legion's exterior re-enable rule, added to the 3.3.5 indoor/outdoor gate.
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
 * @brief Read-only query surface of the outdoor-gate rule (features/wmonative/OutdoorGate).
 *
 * Both engines suppress the exterior pass while the viewer is inside a map-object group -- that part
 * is identical, and so is the indoor test itself (a BSP raycast straight down from the camera, with the
 * same disqualifying mask). What differs is how the exterior gets turned back ON.
 *
 * 3.3.5 has exactly one way: cross a portal into an exterior group. Legion adds three more, and the one
 * that matters here is "the group is indoor AND has transition batches": an open-sided interior such as
 * the underside of a bridge or an archway keeps the world visible. Measured on the active corpus, that
 * holds for 37 of the 94 indoor groups -- every one of them a place where walking in makes half the
 * terrain vanish on 3.3.5 and nothing happen on Legion.
 *
 * The rule is added to the client, not written into the files: the gate is a plain float compared
 * against zero, so raising it in a post-hook of the portal traversal makes the client take its own
 * exterior path with no code patched and nothing reimplemented.
 */
namespace wxl::runtime::wmooutdoor
{
    struct Stats
    {
        uint32_t framesIndoor;      ///< frames the viewer was inside a map object
        uint32_t framesReopened;    ///< frames the exterior was re-enabled by the added rule
        uint32_t framesPortalOpen;  ///< frames stock portal traversal had already re-enabled it
        uint32_t groupResolveFails; ///< frames the viewer group could not be resolved (rule skipped)
        /// Modern "antiportal" groups whose CPU occluders were NOT built. 3.3.5 feeds occluders to an
        /// angular clip buffer with no depth, while modern antiportals are authored for Legion's hi-Z
        /// occlusion buffer -- the bridge's is a 173 x 48 unit slab, which head-on erases the world
        /// behind it and from the side does nothing.
        uint32_t occludersSkipped;
        uint32_t occludersBuilt;    ///< antiportal groups that kept their stock occluders (stock WMOs)
        uint32_t undersideEdges;    ///< occluder edges whose underside was projected into the band
        uint32_t boxesUnculled;     ///< boxes the band proved were below the slab, not behind it
    };

    Stats GetStats();

    /** @brief True when the feature is compiled in (config.hpp kWmoOutdoorGate). */
    bool Enabled();

    /** @brief True once both detours installed; false leaves the stock gate untouched. */
    bool Installed();

    /** @brief Live A/B switch: false restores the stock "portal only" rule without a rebuild. */
    bool RuleEnabled();
    void SetRuleEnabled(bool on);

    /** @brief Fallback switch: true builds no occluder at all for a modern antiportal. */
    bool OccludersSkipped();
    void SetOccludersSkipped(bool on);

    /**
     * @brief The real fix: give the client's occlusion skyline a lower bound.
     *
     * 3.3.5 stores, per azimuth, the highest occluded elevation, and culls anything whose top sits
     * below it -- a model that assumes the occluder is a wall reaching the ground. A modern antiportal
     * is often a floating slab (a bridge deck), so everything visible underneath gets culled too. With
     * this on, the same edges are also projected at the group's floor into a second table, and a box
     * the stock test culled is restored when it proves to be entirely BELOW that floor. It can only
     * ever un-cull, so a mistake costs over-draw and never missing geometry.
     */
    bool UndersideRuleEnabled();
    void SetUndersideRuleEnabled(bool on);
}
