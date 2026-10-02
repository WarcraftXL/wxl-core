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

#include "wxl/game/Binding.hpp"
#include "wxl/offsets/game/World.hpp"

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
        void* wf = Read<void*>(woff::kWorldFrame);
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
     * @brief Casts a ray between two world points and reports what it meets.
     * @param from   World position the ray starts at.
     * @param to     World position it ends at.
     * @param out    Receives the hit; cleared on a miss.
     * @param flags  What the ray collides with; the default is what the client's own cursor pick uses.
     * @return the hit type (0 miss, 2 M2/doodad, 3 terrain/WMO).
     *
     * The screen-facing picks above answer "what is under the cursor". This answers the question a
     * screen has nothing to do with -- what is between two places -- which is what a line of sight, a
     * placement test, or the height of the ground beneath a point all reduce to. It goes to the
     * intersect itself rather than through the cursor hit test, which would drag in a screen
     * projection and the frame that projection reads its camera from.
     *
     * Terrain and map objects only. Model geometry is a separate search in the client and is not
     * covered here, so a ray passes through a tree or a chair.
     */
    inline int TraceLine(const float from[3], const float to[3], WorldHit& out,
                         uint32_t mask = woff::kPickMaskAnything)
    {
        out = WorldHit{};

        // Sized well past the three floats a hit point needs: what the intersect writes here beyond
        // that is not documented, and the client's callers take their position from the fraction
        // instead, which is what this does below.
        float scratch[16] = { 0.0f };
        float fraction = 1.0f;   // the whole segment

        if (!Native<woff::WorldIntersectFn>(woff::kWorldIntersect)(from, to, scratch, &fraction, mask, 0))
            return 0;

        out.type = 3;   // terrain or map object; this entry tests nothing else
        out.pos  = Vec3{ from[0] + (to[0] - from[0]) * fraction,
                         from[1] + (to[1] - from[1]) * fraction,
                         from[2] + (to[2] - from[2]) * fraction };
        out.t    = fraction;
        return out.type;
    }

    /**
     * @brief Finds the surface directly below or above a point.
     * @param x,y    Where on the map to look.
     * @param nearZ  Height to search around; the search spans kGroundSearch either side of it.
     * @param outZ   Receives the surface height.
     * @return True when a surface was found, leaving outZ untouched otherwise.
     *
     * Searches from above rather than from the point itself, so a point already under the ground still
     * reports the ground rather than missing everything below it.
     */
    inline bool GroundZ(float x, float y, float nearZ, float& outZ)
    {
        constexpr float kGroundSearch = 60.0f;

        const float from[3] = { x, y, nearZ + kGroundSearch };
        const float to[3]   = { x, y, nearZ - kGroundSearch };

        WorldHit hit;
        if (!TraceLine(from, to, hit)) return false;
        outZ = hit.pos.z;
        return true;
    }

    /**
     * @brief Reads the engine's live cursor position in device (DDC) pixels.
     * @param ddcX  receives the cursor X.
     * @param ddcY  receives the cursor Y.
     * @return true when the world frame is up; false otherwise.
     */
    inline bool CursorDdc(float& ddcX, float& ddcY)
    {
        void* wf = Read<void*>(woff::kWorldFrame);
        if (!wf) return false;
        void* input = At<void*>(wf, woff::kWorldFrameInput);
        if (!input) return false;

        // This mirrors the engine's own normalized-to-device cursor conversion, applied
        // immediately before its native hit-test call.
        const float ndcX = At<float>(input, woff::kInputCursorNdcX);
        const float ndcY = At<float>(input, woff::kInputCursorNdcY);
        const float ddcWidth = Read<float>(woff::kDdcWidth);
        const float ddcHeight = Read<float>(woff::kDdcHeight);
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
