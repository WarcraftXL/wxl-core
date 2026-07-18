// Clamps source ribbon indices at load and opts the multi-texture ribbon combine at draw.
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

#include "Ribbons.hpp"

#include <cstdint>
#include <cstring>

namespace wxl::modern::assets::m2::ribbons
{
    namespace fmt = wxl::structure::m2;

    namespace
    {
        // The source tail lands in the client layout's padding, so the strides are equal and the slide is a
        // no-op; both names are kept explicit so a future version that grows the body only edits the source.
        constexpr uint32_t kStrideClient = 0xB0;
        constexpr uint32_t kStrideSource = 0xB0;
    }

    /**
     * @brief Clamps every source ribbon's texture/material indices into the header tables.
     *
     * The textureIndices/materialIndices arrays stay at +0x14/+0x1c so the native de-relocator's
     * pointer-fix reads them correctly.
     * @param md        Model header (pre-parse, offsets model-relative).
     * @param fileSize  Total model size, bounding the sub-array reads.
     */
    void Compact(fmt::M2Header* md, uint32_t fileSize)
    {
        if (!md->ribbonEmitters.count || !md->ribbonEmitters.offset) return;
        static_assert(kStrideSource >= kStrideClient, "ribbon forward slide requires dst stride <= src stride");

        uint8_t* arr = md->base() + md->ribbonEmitters.offset;
        for (uint32_t i = 0; i < md->ribbonEmitters.count; ++i)
        {
            uint8_t* s = arr + i * kStrideSource;
            uint8_t* d = arr + i * kStrideClient;
            if (d != s) std::memmove(d, s, kStrideClient);

            auto* rb = reinterpret_cast<fmt::M2Ribbon*>(d);
            if (md->textures.count && rb->textureIndices.offset &&
                static_cast<size_t>(rb->textureIndices.offset) + rb->textureIndices.count * 2 <= fileSize)
            {
                auto* texIdx = reinterpret_cast<uint16_t*>(md->base() + rb->textureIndices.offset);
                for (uint32_t t = 0; t < rb->textureIndices.count; ++t)
                    if (texIdx[t] >= md->textures.count) texIdx[t] = 0;
            }
            if (md->materials.count && rb->materialIndices.offset &&
                static_cast<size_t>(rb->materialIndices.offset) + rb->materialIndices.count * 2 <= fileSize)
            {
                auto* matIdx = reinterpret_cast<uint16_t*>(md->base() + rb->materialIndices.offset);
                for (uint32_t m = 0; m < rb->materialIndices.count; ++m)
                    if (matIdx[m] >= md->materials.count) matIdx[m] = 0;
            }
        }
    }

#ifndef WXL_HOST
    /**
     * @brief Requests the single-pass multi-texture combine at a ribbon draw.
     *
     * The engine draws an N-texture ribbon as N sequential single-texture passes, which cannot
     * reproduce a source ribbon's texture product; the core applies the combine for >= 3 layers.
     * @param a  Ribbon draw arguments.
     */
    void OnRibbonDraw(const wxl::events::RibbonDrawArgs& a)
    {
        *a.useMultiTexture = true;
    }
#endif
}
