// world pick binding: resolve a screen point (or the live cursor) to a world hit position and object.
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

#include "game/Binding.hpp"
#include "offsets/game/World.hpp"

/**
 * @brief Casts a ray from a screen point (or the live cursor) into the world and reports the hit.
 *
 * The pick uses the engine's own screen-to-ray and intersect path, so it agrees with the client's native
 * cursor selection. Valid only in-world; returns a miss otherwise.
 */
namespace wxl::game::world
{
    namespace woff = wxl::offsets::game::world;

    /** @brief A world-space vector (x, y, z). */
    struct Vec3 { float x; float y; float z; };

    /**
     * @brief Result of a world pick.
     *
     * type is 0 for a miss, 2 for an M2/doodad, 3 for terrain or WMO. pos is the world hit point.
     * objLo/objHi is the engine object handle (zero for terrain). t is the distance along the ray.
     */
    struct WorldHit { int type; Vec3 pos; void* objLo; void* objHi; float t; };

    /**
     * @brief Picks the world along the ray through a screen point.
     * @param ddcX  cursor X in device (DDC) pixels.
     * @param ddcY  cursor Y in device (DDC) pixels.
     * @param out   receives the hit; cleared on a miss.
     * @return the hit type (0 miss, 2 M2/doodad, 3 terrain/WMO).
     */
    inline int Pick(float ddcX, float ddcY, WorldHit& out)
    {
        out = WorldHit{};
        void* wf = *reinterpret_cast<void**>(woff::kWorldFrame);
        if (!wf) return 0;

        // result[0..5] = {objLo, objHi, posX, posY, posZ, t}; [6..11] are the near/far ray the call fills.
        int result[12] = { 0 };
        const int type = Native<woff::PickAtScreenFn>(woff::kPickAtScreen)(wf, ddcX, ddcY, woff::kPickModeCursor, result);
        if (type == 0) return 0;

        out.type  = type;
        out.objLo = reinterpret_cast<void*>(result[0]);
        out.objHi = reinterpret_cast<void*>(result[1]);
        out.pos   = *reinterpret_cast<Vec3*>(&result[2]);
        out.t     = *reinterpret_cast<float*>(&result[5]);
        return type;
    }

    /**
     * @brief Reads the engine's live cursor position in device (DDC) pixels.
     * @param ddcX  receives the cursor X.
     * @param ddcY  receives the cursor Y.
     * @return true when the world frame is up; false otherwise.
     */
    inline bool CursorDdc(float& ddcX, float& ddcY)
    {
        void* wf = *reinterpret_cast<void**>(woff::kWorldFrame);
        if (!wf) return false;
        void* input = *reinterpret_cast<void**>(reinterpret_cast<char*>(wf) + woff::kWorldFrameInput);
        if (!input) return false;

        // This is the same NDCToDDC conversion used by CGWorldFrame::SetupDefaultAction
        // immediately before its native HitTestPoint call (WorldFrame.cpp).
        const float ndcX = *reinterpret_cast<float*>(
            reinterpret_cast<char*>(input) + woff::kInputCursorNdcX);
        const float ndcY = *reinterpret_cast<float*>(
            reinterpret_cast<char*>(input) + woff::kInputCursorNdcY);
        const float ddcWidth = *reinterpret_cast<float*>(woff::kDdcWidth);
        const float ddcHeight = *reinterpret_cast<float*>(woff::kDdcHeight);
        if (ddcWidth <= 0.0f || ddcHeight <= 0.0f) return false;

        ddcX = ndcX * ddcWidth;
        ddcY = ndcY * ddcHeight;
        return true;
    }

    /**
     * @brief Picks the world under the current cursor.
     * @param out  receives the hit; cleared on a miss.
     * @return the hit type (0 miss, 2 M2/doodad, 3 terrain/WMO).
     */
    inline int PickCursor(WorldHit& out)
    {
        float x, y;
        if (!CursorDdc(x, y)) { out = WorldHit{}; return 0; }
        return Pick(x, y, out);
    }
}
