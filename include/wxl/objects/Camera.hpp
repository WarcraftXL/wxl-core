// Camera: a handle over the engine's active-camera object, the source the world render reads its
// field of view from. The render state the scene is drawn with -- view, projection, view-projection
// and camera position -- is global and lives here as static members.
// wxl::game::camera::Camera is an unrelated type: a CPU-side struct Aim() fills, not this handle.
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

#include "wxl/game/Camera.hpp"
#include "wxl/game/Pick.hpp"

namespace wxl
{
    using game::world::Vec3;

    /// The engine's active camera, plus the global render state the world scene is drawn with.
    class Camera
    {
    public:
        Camera() = default;

        /**
         * Wraps a raw engine camera pointer.
         *
         * @param void* raw : an active-camera object, may be null
         */
        explicit Camera(void* raw) : raw_(raw) {}

        /**
         * The camera the world render reads.
         *
         * @return Camera camera : null when there is no world frame
         */
        static Camera Active() { return Camera(game::camera::GetActiveCamera()); }

        /// True when the handle holds a camera.
        explicit operator bool() const { return raw_ != nullptr; }

        /**
         * The raw engine pointer, for the bindings that take one.
         *
         * @return void* raw
         */
        void* Raw() const { return raw_; }

        /**
         * The full vertical field of view.
         *
         * @return float fov : radians, a ~70 degree fallback for a null handle
         */
        float Fov() const { return game::camera::GetFov(raw_); }

        /**
         * Writes the full vertical field of view. Ignored for a null handle.
         *
         * @param float fovRad : radians
         *
         * In world the engine sets this from its own state each frame, so it holds only for the frame
         * it is written in.
         */
        void SetFov(float fovRad) const { game::camera::SetFov(raw_, fovRad); }

        /**
         * The world-to-view matrix of the active render state, not a property of any one camera.
         *
         * @return const float* view : row-major float[16], aliasing the engine global
         */
        static const float* View() { return game::camera::GetView(); }

        /**
         * The projection matrix of the active render state, not a property of any one camera.
         *
         * @return const float* projection : row-major float[16], aliasing the engine global
         */
        static const float* Projection() { return game::camera::GetProjection(); }

        /**
         * The combined view-projection matrix of the active render state, not a property of any one
         * camera.
         *
         * @return const float* viewProj : row-major float[16], aliasing the engine global
         */
        static const float* ViewProj() { return game::camera::GetViewProj(); }

        /**
         * The viewer position of the active render state, not a property of any one camera.
         *
         * @return Vec3 position
         */
        static Vec3 Position()
        {
            float p[3];
            game::camera::GetPosition(p);
            return { p[0], p[1], p[2] };
        }

        /**
         * Writes the world-to-view matrix of the active render state, not a property of any one
         * camera.
         *
         * @param const float* m : row-major float[16]
         *
         * In world the engine rewrites it every frame, so a write there is overwritten at once; it
         * aims the scene on the glue screens, where the engine leaves it alone.
         */
        static void SetView(const float m[16]) { game::camera::SetView(m); }

        /**
         * Writes the projection matrix of the active render state, not a property of any one camera.
         *
         * @param const float* m : row-major float[16]
         *
         * Carries the same per-frame rewrite caveat as SetView.
         */
        static void SetProjection(const float m[16]) { game::camera::SetProjection(m); }

        /**
         * Writes the combined view-projection matrix of the active render state, not a property of
         * any one camera.
         *
         * @param const float* m : row-major float[16]; must equal View * Projection or culling
         *                         disagrees with the draw
         *
         * Carries the same per-frame rewrite caveat as SetView.
         */
        static void SetViewProj(const float m[16]) { game::camera::SetViewProj(m); }

        /**
         * Writes the viewer position of the active render state, not a property of any one camera.
         *
         * @param Vec3 position
         *
         * Carries the same per-frame rewrite caveat as SetView.
         */
        static void SetPosition(const Vec3& position)
        {
            const float p[3] = { position.x, position.y, position.z };
            game::camera::SetPosition(p);
        }

        bool operator==(const Camera& c) const { return raw_ == c.raw_; }
        bool operator!=(const Camera& c) const { return raw_ != c.raw_; }

    private:
        void* raw_ = nullptr;
    };
}
