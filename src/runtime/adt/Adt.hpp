// Copyright (C) 2026 WraithEngine
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

// DLL-only. ADT runtime: per-layer terrain texture UV scaling (modern MTXF/MTXP scale the Client ignores).
namespace wraith::runtime::adt
{
    // Install the terrain-constant post-hook that rescales each layer's UV tiling by its texture's scale.
    void Install();

    // Ingest a served .adt: merge any trailing ATSC (UV-scale) and ATHB (height-blend) tables into the global
    // maps and return the ADT-only length (the tables hidden from the Client). Returns size unchanged otherwise.
    uint32_t IngestAdtBytes(const char* name, const uint8_t* buffer, uint32_t size);
}
