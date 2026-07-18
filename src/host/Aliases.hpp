// Texture and model name aliases: the alternate spellings the client may open the same asset under.
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

#include <string>
#include <vector>

// When a direct read of a client name misses, the same asset often lives under a sibling spelling: gender
// and unisex texture variants, semicolon-listed alternates, race/gender object-component forms, collection
// folders, and model extension swaps. These builders enumerate those alternates for the produce pipeline
// to retry. Each derives purely from the name and appends only spellings distinct from it.
namespace wxl::host::produce
{
    /**
     * @brief Appends every alternate spelling `name` may also be opened under, in retry priority order.
     * @param name     requested file name that missed a direct read
     * @param aliases  receives the distinct alternate names to try
     */
    void BuildAliases(const std::string& name, std::vector<std::string>& aliases);
}
