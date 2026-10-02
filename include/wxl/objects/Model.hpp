// Model: a handle over one M2 model object -- its raw .m2 image, parsed header and live skin
// profile. This is the pointer the ModelScript hooks hand an extension: OnModelLoadPre, OnModelLoad,
// OnM2SkinFinalize and OnM2NativeLoad all carry it as `model`. Reads through wxl::game::m2::M2Model;
// the device, scene and allocator calls of wxl::game::m2 stay free functions.
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

#include "wxl/formats/M2Format.hpp"
#include "wxl/game/M2.hpp"

namespace wxl
{
    /// One M2 model object: the .m2 image the loader read, the header the parse produced, the skin
    /// the finalize attached.
    class Model
    {
    public:
        Model() = default;

        /**
         * Wraps a raw M2 model pointer.
         *
         * @param void* raw : the model object, may be null
         */
        explicit Model(void* raw) : raw_(raw) {}

        /// True when the handle holds a model.
        explicit operator bool() const { return raw_ != nullptr; }

        /**
         * The raw client pointer, for the bindings that take one.
         *
         * @return void* raw
         */
        void* Raw() const { return raw_; }

        /**
         * The raw .m2 file buffer the loader read the whole file into.
         *
         * @return void* buffer : the parse reads and rewrites it in place
         */
        void* FileBuffer() const { return game::m2::M2Model(raw_).GetFileBuffer(); }

        /**
         * The byte size of the raw .m2 file buffer.
         *
         * @return uint32 size
         */
        uint32_t FileSize() const { return game::m2::M2Model(raw_).GetFileSize(); }

        /**
         * The model path stem stored by the native loader.
         *
         * @return string stem
         */
        const char* PathStem() const { return game::m2::M2Model(raw_).GetPathStem(); }

        /**
         * The parsed model header. The .m2 buffer is parsed in place, so the buffer base is the
         * header. Valid once the model is parsed, at which point the header's M2Arrays hold raw
         * pointers rather than file offsets.
         *
         * @return M2Header* header
         */
        wxl::structure::m2::M2Header* Header() const { return game::m2::M2Model(raw_).GetHeader(); }

        /**
         * The bounding sphere radius, the standard MD20 field. Valid once the model is parsed.
         *
         * @return float radius
         */
        float BoundingSphereRadius() const { return game::m2::M2Model(raw_).GetBoundingSphereRadius(); }

        /**
         * The particle emitter count, read from the parsed header. Valid once the model is parsed.
         *
         * @return uint32 count
         */
        uint32_t ParticleEmitterCount() const { return game::m2::M2Model(raw_).GetParticleEmitterCount(); }

        /**
         * The engine's live parsed skin profile. Not the on-disk skin: its arrays sit 4 bytes higher
         * than the file layout and its count/pointer pairs are raw pointers, with indices global
         * into the model vertex and index pools.
         *
         * @return M2SkinProfile* skin : null before the skin is attached, so valid at or after skin
         *         finalize
         */
        game::m2::M2SkinProfile* Skin() const { return game::m2::M2Model(raw_).GetSkin(); }

        /**
         * Points the model at a different .m2 image. The parser reads both fields on its next call.
         *
         * @param void* buffer : the new .m2 image
         * @param uint32 size : its byte size
         */
        void SetBuffer(void* buffer, uint32_t size) { game::m2::M2Model(raw_).SetBuffer(buffer, size); }

        /**
         * Rebuilds the GPU index buffer from the model's current rawTri, the skin's indices. Called
         * directly when the skin profile is already present and the index buffer needs rebuilding
         * with an updated geoset filter. Accepts small per-call leaks of internal buffers as the cost
         * of forcing a re-bake, and always resets rawTri to identity after building the index buffer,
         * so a filter is applied immediately before the call.
         */
        void FinalizeSkin() { game::m2::M2Model(raw_).FinalizeSkin(); }

        bool operator==(const Model& o) const { return raw_ == o.raw_; }
        bool operator!=(const Model& o) const { return raw_ != o.raw_; }

    private:
        void* raw_ = nullptr;
    };
}
