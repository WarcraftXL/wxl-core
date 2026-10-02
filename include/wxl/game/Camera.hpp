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

#include "wxl/game/Binding.hpp"
#include "wxl/offsets/engine/Camera.hpp"

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
    inline const float* GetView()       { return reinterpret_cast<const float*>(off::kView); }
    /**
     * @brief Reads the projection matrix.
     * @return Pointer to a row-major float[16].
     */
    inline const float* GetProjection() { return reinterpret_cast<const float*>(off::kProjection); }
    /**
     * @brief Reads the combined view-projection matrix (View * Projection).
     * @return Pointer to a row-major float[16].
     */
    inline const float* GetViewProj()   { return reinterpret_cast<const float*>(off::kViewProj); }

    /**
     * @brief Reads the camera world position.
     * @param out  Receives the position in out[0..2].
     */
    inline void GetPosition(float out[3])
    {
        const float* p = reinterpret_cast<const float*>(off::kCameraPos);
        out[0] = p[0]; out[1] = p[1]; out[2] = p[2];
    }

    /**
     * @brief Returns the engine's active-camera object (a distinct object from the view/projection
     *        globals above -- the source the world render reads its field of view from).
     * @return the active camera, or null if none is set.
     */
    inline void* GetActiveCamera()
    {
        return Native<off::GetActiveCameraFn>(off::kGetActiveCamera)();
    }

    /**
     * @brief Reads the field of view off an active-camera object.
     * @param camera  an object from GetActiveCamera(), possibly null.
     * @return the camera's field of view in radians, or a ~70 degree fallback when camera is null.
     */
    inline float GetFov(void* camera)
    {
        return camera
            ? *reinterpret_cast<const float*>(static_cast<const uint8_t*>(camera) + off::kCameraFov)
            : 1.2217f; // ~70 degrees; fallback for the no-camera case
    }

    // The setters below drive the world renderer from a camera the engine does not own. In-world the
    // engine rewrites all four every frame from its own camera, so writing them there is overwritten
    // immediately; they are the way to aim the scene on the glue screens, where the engine leaves them
    // at identity and never touches them again.
    //
    // Layout is what the engine builds: row-major float[16], D3D row-vector, left-handed. Projection
    // index 0 = X scale, 5 = Y scale, 10 = Z scale, 11 = 1, 14 = Z bias.

    /**
     * @brief Writes the world-to-view matrix.
     * @param m  Row-major float[16].
     */
    inline void SetView(const float m[16])
    { for (int i = 0; i < 16; ++i) reinterpret_cast<float*>(off::kView)[i] = m[i]; }

    /**
     * @brief Writes the projection matrix.
     * @param m  Row-major float[16].
     */
    inline void SetProjection(const float m[16])
    { for (int i = 0; i < 16; ++i) reinterpret_cast<float*>(off::kProjection)[i] = m[i]; }

    /**
     * @brief Writes the combined view-projection matrix.
     * @param m  Row-major float[16]; must equal View * Projection or culling disagrees with the draw.
     */
    inline void SetViewProj(const float m[16])
    { for (int i = 0; i < 16; ++i) reinterpret_cast<float*>(off::kViewProj)[i] = m[i]; }

    /**
     * @brief Writes the camera world position.
     * @param pos  Position x, y, z.
     */
    inline void SetPosition(const float pos[3])
    {
        float* p = reinterpret_cast<float*>(off::kCameraPos);
        p[0] = pos[0]; p[1] = pos[1]; p[2] = pos[2];
    }

    // --- the per-frame matrix build, as a detour target ---
    // The view and projection globals above are rewritten every frame in world, so writing them there
    // is overwritten before anything reads them. This is where that rewrite happens, and it is a
    // look-at: the eye and the point it looks at go in, and everything the scene is drawn with comes
    // out. A detour that adjusts the eye before calling through therefore moves the camera without
    // owning the projection, the near and far planes, or the culling agreement that keeping View and
    // ViewProj in step by hand would demand.
    //
    // Confirmed by disassembly 2026-09-11: at 0x007954A6 the entry copies three floats from the first
    // argument into kCameraPos and three from the second into the slot above it, then subtracts the
    // first from the second to get the view direction. Its one caller (0x007832ED) cleans eight bytes
    // off the stack after the call, which fixes it as __cdecl with two arguments.

    /// Entry the world's view and projection are built by, once per frame.
    constexpr uintptr_t kBuildMatricesSeam = off::kBuildCameraMatrices;

    /// Its signature: (eye, target), both float[3] in world coordinates.
    using BuildMatricesFn = void(__cdecl*)(const float eye[3], const float target[3]);

    /// The name the core registers kBuildMatricesSeam under, for HookAttachByName.
    inline constexpr char kBuildMatricesPoint[] = "Camera.BuildMatrices";

    /**
     * @brief Writes the field of view on an active-camera object.
     * @param camera  an object from GetActiveCamera(); ignored when null.
     * @param fovRad  full vertical field of view in radians.
     *
     * The counterpart to GetFov. In world the engine sets this from its own state each frame, so it
     * holds only for the frame it is written in and belongs wherever that frame is being shaped.
     */
    inline void SetFov(void* camera, float fovRad)
    {
        if (camera) *reinterpret_cast<float*>(static_cast<uint8_t*>(camera) + off::kCameraFov) = fovRad;
    }

    // --- supplying a camera the world renderer accepts ---
    // The world scene render takes its camera from the world frame and dereferences it without
    // checking. In world there always is one; anywhere else there is not, and it has to be lent one.

    /**
     * @brief A camera the world renderer accepts.
     *
     * It is read through four methods -- field of view and the three basis vectors -- and Init points
     * it at the engine's own implementations of them, so what answers the renderer is the engine's
     * code reading these fields rather than a stand-in imitating it.
     */
    using Camera = off::SimpleCamera;

    /**
     * @brief Prepares a camera and aims it.
     * @param cam      Camera to fill; every field is overwritten.
     * @param pos      World position x, y, z.
     * @param forward  Unit forward vector.
     * @param right    Unit right vector.
     * @param up       Unit up vector.
     * @param fovRad   Full vertical field of view, radians.
     */
    inline void Aim(Camera& cam, const float pos[3], const float forward[3],
                    const float right[3], const float up[3], float fovRad)
    {
        cam.vtable = reinterpret_cast<const void*>(off::kSimpleCameraVTable);
        for (int i = 0; i < 3; ++i)
        {
            cam.position[i] = pos[i];
            cam.forward[i]  = forward[i];
            cam.right[i]    = right[i];
            cam.up[i]       = up[i];
        }
        cam.fov = fovRad;
    }
}
