// MD21 container: de-chunk a chunked modern M2 to a self-contained MD20 the Client loader reads.
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
#include <span>
#include <string>
#include <vector>

// A modern M2 ships as an MD21 chunked container: a leading MD21 header (4-byte tag + MD20 block size), the
// MD20 model block, then auxiliary chunks (SFID skin ids, TXID texture ids, ...). The Client loader reads
// only a self-contained MD20 whose textures name their files inline. This extracts the MD20 block and
// inlines the texture names the TXID chunk references by FileDataID, so the de-chunked image is what the
// downport then compacts onto the Client contract.
namespace wxl::modern::assets::m2::md21
{
    // FileDataID -> path. Returns false when the id does not resolve.
    using TxidResolver = bool (*)(uint32_t fileDataId, std::string& outPath);

    // Reports whether `in` is an MD21 chunked container.
    bool IsMd21(std::span<const uint8_t> in);

    // Extract the MD20 block from an MD21 container and inline the TXID-referenced texture names. `resolve`
    // maps a texture FileDataID to a path. Returns false when `in` is not an MD21 container or is malformed.
    bool Dechunk(std::span<const uint8_t> in, TxidResolver resolve, std::vector<uint8_t>& out);

    // Zero the bone lookup table (boneCombos) so a static prop's shadow stops swinging with the camera.
    // Scoped to models whose boneCombos looks like that case (few distinct bones, none billboard); a real
    // skeleton (creature/character) is left untouched. Operates on an MD20 image in place.
    void ZeroBoneLookup(uint8_t* md20, uint32_t size);
}
