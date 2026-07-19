// Legion-style height-based terrain texture blending: runtime settings + stats surface.
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

/// Runtime tuning + status surface of the terrain height-blend feature (features/adtsplit/
/// HeightBlend.cpp), exposed to Lua as part of wxl.adt.*. The settings are plain structs mutated on
/// the game thread (the draw detour reads them per chunk); no game memory is touched through here.
namespace wxl::features::heightblend
{
    /** @brief Live tunables of the height-blend draw (seeded from WarcraftXL.cfg at install). */
    struct Settings
    {
        bool  enabled    = true;  ///< runtime master switch (WXL_ADT_HEIGHT_BLEND)
        float sharpness  = 2.0f;  ///< sharpen strength in w *= 1-sat((max(w)-w)*sharpness); calibrated on trollraid
                                  ///< (WXL_ADT_HEIGHT_SHARPNESS; 0 = pure MAD+normalize blend)
        bool  channelRed = false; ///< read the _h height from .r instead of .a
                                  ///< (WXL_ADT_HEIGHT_CHANNEL = "r"; for alpha-less DXT1 repacks)
    };

    /** @brief Returns the mutable live settings (game thread). */
    Settings& Get();

    /** @brief Session counters + activity flag. */
    struct Stats
    {
        uint32_t patchedShaders; ///< stock PS permutations successfully patched (cached)
        uint32_t patchFailures;  ///< permutations that could not be patched (drawn stock)
        uint32_t chunksDrawn;    ///< chunk draws that went through the height-blend path
        bool     active;         ///< the fast-path gate is currently open (map + tiles + toggle)
    };

    /** @brief Returns a snapshot of the session counters. */
    Stats GetStats();

    /** @brief True once the draw-leaf detour is installed. */
    bool Installed();

    /**
     * @brief Drops every cached patched pixel shader so the next draw rebuilds them (used after a
     *        height-channel change, which alters the injected code). Game/draw thread only.
     */
    void InvalidateShaders();
}
