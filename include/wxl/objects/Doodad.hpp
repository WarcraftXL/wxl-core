// Doodad: a handle over one placed map doodad, an entry of a terrain chunk's doodad list rather than
// a GUID-bearing resident object. One pointer, owns nothing, reads and writes through the
// wxl::game::doodad bindings.
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

#include <cstddef>

#include "wxl/game/Doodad.hpp"
#include "wxl/objects/Object.hpp"

namespace wxl
{
    /// A placed map doodad: a model, a world transform, and the bounds the client picks it by.
    class Doodad
    {
    public:
        /// A model file name with its own storage, so reading one needs no caller buffer.
        struct Name
        {
            /// Bytes of storage, terminator included; a longer name is truncated to fit.
            static constexpr size_t kCapacity = 128;

            char text[kCapacity] = { '\0' };

            /**
             * The bare model file name, without its directory.
             *
             * @return const char* text : empty when the name could not be read
             */
            const char* Text() const { return text; }

            /// True when a name was read.
            explicit operator bool() const { return text[0] != '\0'; }
        };

        Doodad() = default;

        /**
         * Wraps a raw doodad pointer.
         *
         * @param void* raw : the client doodad, may be null
         */
        explicit Doodad(void* raw) : raw_(raw) {}

        /**
         * True when the handle holds a live doodad: its position block is mapped and reads as a sane
         * world location. A null or stale pointer reports false.
         */
        explicit operator bool() const { return game::doodad::IsValid(raw_); }

        /**
         * The raw client pointer, for the bindings that take one.
         *
         * @return void* raw
         */
        void* Raw() const { return raw_; }

        /**
         * The render instance the doodad draws through. The live world matrix the renderer reads each
         * frame lives on the instance, not on the doodad.
         *
         * @return void* instance : null while the model is still loading
         */
        void* Instance() const { return game::doodad::Instance(raw_); }

        /**
         * The bare model file name, copied into a caller buffer.
         *
         * @param char* out : receives the name, always terminated
         * @param size_t cap : capacity of out in bytes
         * @return bool ok : false when the model is not loaded yet or the buffer is unusable
         */
        bool ModelName(char* out, size_t cap) const { return game::doodad::ModelName(raw_, out, cap); }

        /**
         * The bare model file name, in storage of its own.
         *
         * @return Name name : falsy when the model is not loaded yet
         */
        Name ModelName() const
        {
            Name n;
            game::doodad::ModelName(raw_, n.text, Name::kCapacity);
            return n;
        }

        /**
         * The world position.
         *
         * @return Vec3 position : the origin when the block is not mapped
         */
        Vec3 Position() const
        {
            float p[3];
            game::doodad::Position(raw_, p);
            return { p[0], p[1], p[2] };
        }

        /**
         * The uniform scale.
         *
         * @return float scale : 1.0 when the field is not mapped
         */
        float Scale() const { return game::doodad::Scale(raw_); }

        /**
         * The cursor-pick target: the world bounding-sphere center.
         *
         * @return Vec3 center : the position when that field reads unmapped or implausibly far from it
         */
        Vec3 Center() const
        {
            float c[3];
            game::doodad::Center(raw_, c);
            return { c[0], c[1], c[2] };
        }

        /**
         * The doodad's own world-space bounding box, a degenerate point set once at spawn.
         *
         * @param Vec3 min : receives the box minimum
         * @param Vec3 max : receives the box maximum
         * @return bool ok : false when the block is not mapped, leaving min and max untouched
         */
        bool BBox(Vec3& min, Vec3& max) const
        {
            float mn[3], mx[3];
            if (!game::doodad::BBox(raw_, mn, mx)) return false;
            min = { mn[0], mn[1], mn[2] };
            max = { mx[0], mx[1], mx[2] };
            return true;
        }

        /**
         * The model-LOCAL bounding box from the parsed model header, the model's real extents.
         * Transform its 8 corners by WorldMatrix to get the world box of this placement.
         *
         * @param Vec3 lo : receives the box minimum
         * @param Vec3 hi : receives the box maximum
         * @return bool ok : false while the model loads or when the box reads degenerate, leaving lo
         *                   and hi untouched
         */
        bool LocalBounds(Vec3& lo, Vec3& hi) const
        {
            float l[3], h[3];
            if (!game::doodad::LocalBounds(raw_, l, h)) return false;
            lo = { l[0], l[1], l[2] };
            hi = { h[0], h[1], h[2] };
            return true;
        }

        /**
         * The live world matrix the renderer reads each frame.
         *
         * @param float m : receives 16 floats, row-major with the translation in m[12..14]
         * @return bool ok : false when the model is not loaded or the matrix is not mapped
         */
        bool WorldMatrix(float m[16]) const { return game::doodad::WorldMatrix(raw_, m); }

        /**
         * Replaces the whole world transform, instance matrix first, then the doodad staging copies.
         *
         * @param float m : source matrix of 16 floats, row-major
         */
        void SetWorldMatrix(const float m[16]) const
        {
            if (raw_) game::doodad::SetWorldMatrix(raw_, m);
        }

        /**
         * Moves the doodad, rewriting the translation row of the live instance matrix.
         *
         * @param Vec3 position : the new world position
         */
        void SetPosition(const Vec3& position) const
        {
            const float p[3] = { position.x, position.y, position.z };
            if (raw_) game::doodad::SetPosition(raw_, p);
        }

        /**
         * Moves the doodad from a plain coordinate triple.
         *
         * @param float p : the new world position in p[0..2]
         */
        void SetPosition(const float p[3]) const
        {
            if (raw_) game::doodad::SetPosition(raw_, p);
        }

        bool operator==(const Doodad& d) const { return raw_ == d.raw_; }
        bool operator!=(const Doodad& d) const { return raw_ != d.raw_; }

    protected:
        void* raw_ = nullptr;
    };
}
