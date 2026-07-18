// .skel merge: splice a Legion+ split-skeleton file's bones/sequences/attachments back into an MD20 image.
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
#include <vector>

// A .skel-based source M2 (SKID chunk present) ships its bones (SKB1), sequences+globalLoops+sequenceLookup
// (SKS1) and attachments (SKA1) in a sibling .skel file instead of the MD20 body -- those header arrays are
// (0,0) in the source. The native loader has no concept of .skel at all: it reads bones/sequences straight
// out of the MD20 buffer at fixed header offsets. This splices each chunk's payload onto the MD20 tail and
// points the corresponding header array at it, so the loader sees one self-contained image either way.
namespace wxl::modern::assets::m2::skel
{
    // Merges SKS1/SKB1/SKA1 chunks found in `skelFile` onto the tail of `md20`, growing it in place. Only
    // fills a header array that is empty (0,0) in `md20`; a model that already carries its own data for one
    // of these arrays keeps it. Returns true if anything was merged.
    bool Merge(std::span<const uint8_t> skelFile, std::vector<uint8_t>& md20);
}
