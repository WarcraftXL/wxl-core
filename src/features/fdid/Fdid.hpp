// FileDataID resolver: turns a modern texture/model FileDataID into a client path via custom DB2s.
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

/**
 * @brief FileDataID -> path service, backed by the client's custom TextureFilePath / ModelFilePath
 *        DB2s (WDC1). Retail resolves a FileDataID straight to bytes through the CASC root; a 3.3.5
 *        MPQ install has no such index, so these community-listfile-built tables ARE the bridge.
 */
namespace wxl::fdid
{
    /// Texture FileDataID (MDID/MHID) -> backslash path, or nullptr when unresolved.
    const char* ResolveTexture(uint32_t fileDataId);

    /// Model FileDataID (MDDF/MODF) -> backslash path, or nullptr when unresolved.
    const char* ResolveModel(uint32_t fileDataId);

    /// True once the texture table is loaded and non-empty (for diagnostics / Lua status).
    bool TextureTableReady();
    bool ModelTableReady();
    uint32_t TextureRecordCount();
    uint32_t ModelRecordCount();
}
