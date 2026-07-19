// Schema: ModelFilePath.db2 -- maps a model FileDataID to its client path.
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
 * @brief Custom DB2 built from the community listfile: FileDataID -> model path (.m2 / .wmo).
 *
 * Same layout as TextureFilePath (2 fields, 8-byte records, id inline, field 1 = path string,
 * id-sorted). Legion+ ADTs reference placed doodads (MDDF) and map objects (MODF) by FileDataID;
 * this table turns those ids back into a path the by-name model loader can open -- the counterpart
 * to TextureFilePath that will let the split-tile object layer resolve (flip kAdtSplitSkipObjects).
 */
namespace wxl::db2::schema
{
    struct ModelFilePath
    {
        static constexpr const char* kFile          = "DBFilesClient\\ModelFilePath.db2";
        static constexpr uint32_t    kFieldFilePath = 1; // string; id is read at id_index

        uint32_t    fileDataId = 0;
        const char* filePath   = "";

        static ModelFilePath Read(const Wdc1& db, uint32_t rec)
        {
            return { db.Id(rec), db.Str(rec, kFieldFilePath) };
        }
    };
}
