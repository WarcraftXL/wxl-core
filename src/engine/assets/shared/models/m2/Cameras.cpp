// Compacts the source camera record onto the client camera stride.
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

#include "Cameras.hpp"

#include <cstdint>
#include <cstring>

namespace wxl::modern::assets::m2::cameras
{
    namespace fmt = wxl::structure::m2;

    namespace
    {
        constexpr float kCameraFov = 0.7853982f; // 45deg, substituted for the fov float source cameras drop

#pragma pack(push, 1)
        // 16 bytes larger than the client camera: the explicit fov float is gone and a FoV track is appended.
        struct M2CameraSource
        {
            uint32_t         type;           // 0x00
            float            farClip;        // 0x04
            float            nearClip;       // 0x08
            fmt::M2CameraBody body;          // 0x0C
            uint8_t          fovTrack[0x14]; // 0x60  (appended FoV track)
        };
#pragma pack(pop)
        static_assert(sizeof(M2CameraSource) == 0x74, "M2CameraSource");
    }

    /**
     * @brief Compacts every source camera onto the client camera stride in place.
     *
     * dst stride (0x64) < src stride (0x74), so the forward walk never overwrites a camera before it
     * is read.
     * @param md  Model header (pre-parse, offsets model-relative).
     */
    void Compact(fmt::M2Header* md)
    {
        if (!md->cameras.count || !md->cameras.offset) return;
        uint8_t* arr = md->base() + md->cameras.offset;
        for (uint32_t i = 0; i < md->cameras.count; ++i)
        {
            auto* s = reinterpret_cast<M2CameraSource*>(arr + i * sizeof(M2CameraSource));
            auto* d = reinterpret_cast<fmt::M2Camera*>(arr + i * sizeof(fmt::M2Camera));

            uint32_t type     = s->type;
            float    farClip  = s->farClip;
            float    nearClip = s->nearClip;
            std::memmove(&d->body, &s->body, sizeof(fmt::M2CameraBody));
            d->type     = type;
            d->fov      = kCameraFov;
            d->farClip  = farClip;
            d->nearClip = nearClip;
        }
    }
}
