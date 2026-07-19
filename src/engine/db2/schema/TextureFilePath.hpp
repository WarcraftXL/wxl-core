// Schema: TextureFilePath.db2 -- maps a texture FileDataID to its client path.
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

#include "engine/db2/Wdc1.hpp"

/**
 * @brief Custom DB2 built from the community listfile: FileDataID -> texture path.
 *
 * Layout (verified): 2 fields, 8-byte fixed records, id (FileDataID) inline at id_index (field 0),
 * field 1 = string-table offset of the backslash path ("tileset\\expansion06\\...\\x_s.blp"),
 * records sorted ascending by FileDataID. Legion+ ADTs reference terrain textures by these ids in
 * MDID (diffuse) / MHID (height); this table is how the client turns a modern FileDataID back into
 * a path its by-name loader (CMap::LoadTexture) can open.
 */
namespace wxl::db2::schema
{
    struct TextureFilePath
    {
        static constexpr const char* kFile          = "DBFilesClient\\TextureFilePath.db2";
        static constexpr uint32_t    kFieldFilePath = 1; // string; id is read at id_index

        uint32_t    fileDataId = 0;
        const char* filePath   = "";

        static TextureFilePath Read(const Wdc1& db, uint32_t rec)
        {
            return { db.Id(rec), db.Str(rec, kFieldFilePath) };
        }
    };
}
