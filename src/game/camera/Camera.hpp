// camera bindings: the live world view / projection matrices and camera position.
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
#include "offsets/engine/Camera.hpp"

/**
 * @brief Typed accessors for the live world view/projection matrices and camera position.
 *
 * The returned pointers alias the engine globals directly and are valid only in-world.
 */
namespace wxl::game::camera
{
    namespace off = wxl::offsets::engine::camera;

    /**
     * @brief Reads the world-to-view matrix.
     * @return Pointer to a row-major float[16] (D3D row-vector).
     */
    inline const float* View()       { return reinterpret_cast<const float*>(off::kView); }
    /**
     * @brief Reads the projection matrix.
     * @return Pointer to a row-major float[16].
     */
    inline const float* Projection() { return reinterpret_cast<const float*>(off::kProjection); }
    /**
     * @brief Reads the combined view-projection matrix (View * Projection).
     * @return Pointer to a row-major float[16].
     */
    inline const float* ViewProj()   { return reinterpret_cast<const float*>(off::kViewProj); }

    /**
     * @brief Reads the camera world position.
     * @param out  Receives the position in out[0..2].
     */
    inline void Position(float out[3])
    {
        const float* p = reinterpret_cast<const float*>(off::kCameraPos);
        out[0] = p[0]; out[1] = p[1]; out[2] = p[2];
    }

    /** @brief Adds the camera bindings to the enumerable catalog. */
    inline void RegisterCatalog()
    {
        Register({ "Camera::View",       off::kView,       "const float[16] (world->view)" });
        Register({ "Camera::Projection", off::kProjection, "const float[16]" });
        Register({ "Camera::ViewProj",   off::kViewProj,   "const float[16]" });
    }
}
