// Source contract: the source-only chunks the translate strips and the field deltas it normalizes.
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

#include "WmoClient.hpp"

// Source contract: what a source WMO ADDS or encodes differently relative to the Client shape in
// WmoClient.hpp. Shared tags (MVER, kept root/group chunks) live there; this header declares only the
// source-only chunks the translate strips, the field deltas it normalizes, and the source flag bits it
// clears. The translate is data-gated (keyed off chunk presence / field values), not versioned.
namespace wxl::modern::assets::wmo
{
    // Source-only root chunks the Client loader has no slot for; stripped before the positional walk.
    constexpr uint32_t kGFID = FourCC('G', 'F', 'I', 'D'); // group FileDataIDs
    constexpr uint32_t kMOUV = FourCC('M', 'O', 'U', 'V'); // animated UV translations
    constexpr uint32_t kMODI = FourCC('M', 'O', 'D', 'I'); // doodad FileDataIDs
    constexpr uint32_t kMOSI = FourCC('M', 'O', 'S', 'I'); // skybox FileDataID

    // Newer-source group chunks (group rewrite). MPY2 is MOPY v2 (4B/tri); it is converted back to MOPY.
    // MOGX is the new leading sub-chunk and only marks the layout. Other new-only group chunks
    // (MOQG/MOC2/...) carry nothing the Client render path reads and are dropped.
    constexpr uint32_t kMPY2 = FourCC('M', 'P', 'Y', '2'); // material/poly info v2 (-> MOPY)
    constexpr uint32_t kMOGX = FourCC('M', 'O', 'G', 'X'); // ground-type query start (new first sub-chunk)

    // Source shader ids above the Client's 0..6 collapse to 0 (Diffuse on texture_1). Shader 23 keeps its
    // real diffuse in texture_2 (texture_1 is a shared effects map), so the translate promotes tex2 first.
    constexpr uint32_t kShaderDFSurface = 23;

    // Shader ids whose dropped layer would become an additive overlay (deferred to the multipass work).
    // 23: texture_1 is the additive shine. 9 (DiffuseEmissive): texture_2 is the additive emissive.
    constexpr uint32_t kShaderDiffuseEmissive = 9;

    // Placeholder name written when a texture FileDataID does not resolve, keeping the material's MOTX
    // offset pointed at a valid NUL-terminated string.
    constexpr const char kFallbackTexture[] = "createcrappygreentexture.blp";

    // MOBA material-id relocation. When the flag byte at +0x16 has bit 0x2 set, the real material id sits
    // at +0x0A and the leading bytes overlap what the Client reads as the i16 bounding box. The translate
    // moves the id to the Client offset, clears the flag, and rebuilds the bounding box.
    constexpr uint32_t kMobaModernMatOff = 0x0A; // u8 source material id
    constexpr uint32_t kMobaFlagOffset   = 0x16; // u8 relocation flag
    constexpr uint8_t  kMobaRelocFlag    = 0x02;
}
