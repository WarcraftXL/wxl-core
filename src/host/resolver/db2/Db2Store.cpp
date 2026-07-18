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

#include "Db2Store.hpp"

#include "mpq/MpqStore.hpp"
#include "common/Log.hpp"

#include "DB2File.hpp"

#include <cstdint>
#include <vector>

using wxl::features::db2::DB2File;
using wxl::host::mpq::MpqStore;

namespace
{
    // Read DBFilesClient\<name> through the host archive system and decode it. A table the archives
    // do not carry (or that fails to decode) is logged and skipped - the host degrades rather than
    // hard-failing on one table. No filesystem path is ever built; the name flows through the MPQ set
    // exactly like any other asset.
    bool LoadTable(const MpqStore& mpq, const char* name, DB2File& table)
    {
        std::vector<uint8_t> buf;
        if (!mpq.ReadAll(std::string("DBFilesClient\\") + name, buf) || buf.empty())
        {
            WLOG_INFO("db2: %s not in archives, skipped", name);
            return false;
        }
        return table.LoadBytes(buf.data(), static_cast<uint32_t>(buf.size()), name);
    }
}

namespace wxl::host::db2
{
    bool Db2Store::Load(const MpqStore& mpq)
    {
        // The resolution tables the host supports, read BY NAME through the archive system. Each is
        // optional: a missing/unsupported table is skipped, resolution just degrades for it.
        LoadTable(mpq, "TextureFilePath.db2", m_tex);
        LoadTable(mpq, "ModelFilePath.db2",   m_model);
        LoadTable(mpq, "TextureFileData.db2", m_texData);

        for (uint32_t i = 0; i < m_texData.RowCount(); ++i)
        {
            const TexDataRow* r = m_texData.At(i);
            if (r) m_mridIndex[r->materialResId].push_back({ r->textureType, r->fileDataId });
        }

        WLOG_INFO("db2: loaded texpath=%u model=%u texdata=%u (MRID index=%zu)",
            m_tex.RowCount(), m_model.RowCount(), m_texData.RowCount(), m_mridIndex.size());

        // FDID resolution is possible as long as at least one path table loaded.
        return m_tex.RowCount() != 0 || m_model.RowCount() != 0;
    }

    bool Db2Store::ResolveFile(uint32_t fileDataId, std::string& outPath) const
    {
        const PathRow* r = m_tex.Find(static_cast<int32_t>(fileDataId));
        const char* path = r ? m_tex.Str(static_cast<uint32_t>(r->path)) : nullptr;
        if (!path)
        {
            r = m_model.Find(static_cast<int32_t>(fileDataId));
            path = r ? m_model.Str(static_cast<uint32_t>(r->path)) : nullptr;
        }
        if (!path) return false;
        outPath = path;
        return true;
    }

    uint32_t Db2Store::MridToFdid(uint32_t mrid, uint32_t want) const
    {
        auto it = m_mridIndex.find(mrid);
        if (it == m_mridIndex.end()) return 0;
        for (uint32_t target : { want, 2u })
            for (const auto& c : it->second)
                if (c.first == target) return c.second;
        return it->second[0].second;
    }

    bool Db2Store::ResolveMaterial(uint32_t mrid, uint32_t typeHint, std::string& outPath) const
    {
        uint32_t fdid = MridToFdid(mrid, typeHint);
        if (!fdid) return false;
        const PathRow* r = m_tex.Find(static_cast<int32_t>(fdid));
        const char* path = r ? m_tex.Str(static_cast<uint32_t>(r->path)) : nullptr;
        if (!path) return false;
        outPath = path;
        return true;
    }
}
