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
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "DB2File.hpp"

namespace wxl::host::mpq { class MpqStore; }

// The resolution AUTHORITY. Loads the path tables
// (TextureFilePath, ModelFilePath, TextureFileData) and answers FDID/MRID -> path. The
// DLL caches results over IPC; this side owns the data. WDC1/2/3 decode is shared.
namespace wxl::host::db2
{
    class Db2Store
    {
    public:
        // Read the supported resolution tables BY NAME through the host archive system (mpq). A
        // table the archives lack (or that fails to decode) is skipped, not fatal. No paths.
        bool Load(const wxl::host::mpq::MpqStore& mpq);

        // FileDataID -> file path. Tries the texture table, then the model table.
        bool ResolveFile(uint32_t fileDataId, std::string& outPath) const;

        // MaterialResourcesID (+ texture-type hint) -> .blp path.
        bool ResolveMaterial(uint32_t mrid, uint32_t typeHint, std::string& outPath) const;

    private:
        // Both *FilePath.db2 decode to {uint32 id; int32 path}.
        struct PathRow { uint32_t id; int32_t path; };
        // TextureFileData.db2 -> {FileDataID, MaterialResourcesID, textureType, relMRID}.
        struct TexDataRow { uint32_t fileDataId; uint32_t materialResId; uint32_t textureType; uint32_t rel; };

        // MaterialResourcesID -> FileDataID, preferring textureType == want, then 2, then any.
        uint32_t MridToFdid(uint32_t mrid, uint32_t want) const;

        wxl::features::db2::DB2Table<PathRow>    m_tex;
        wxl::features::db2::DB2Table<PathRow>    m_model;
        wxl::features::db2::DB2Table<TexDataRow> m_texData;
        std::unordered_map<uint32_t, std::vector<std::pair<uint32_t, uint32_t>>> m_mridIndex;
    };
}
