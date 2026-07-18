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
#include "DB2File.hpp"

#include "DB2.hpp"
#include "common/Log.hpp"

using namespace wxl;

namespace wxl::features::db2
{
    DB2File::DB2File(uint32_t rowSize) : m_rowSize(rowSize) {}

    bool DB2File::LoadBytes(const uint8_t* data, uint32_t size, const char* nameForLog)
    {
        if (m_loaded) return true;
        if (!data || size < 4)
        {
            WLOG_WARN("DB2: empty/short data for %s", nameForLog);
            return false;
        }

        uint32_t strColCount = 0;
        const uint32_t* strCols = StringColumns(&strColCount);

        DB2Decoded dec;
        if (!DecodeDB2(data, size, dec, strCols, strColCount))
        {
            WLOG_ERROR("DB2: %s is not a supported DB2 (WDC1/2/3) / malformed", nameForLog);
            return false;
        }

        if (dec.rowSize != m_rowSize)
        {
            WLOG_ERROR("DB2: %s decoded record size %u != definition %u%s", nameForLog, dec.rowSize, m_rowSize,
                       dec.hasRelationship ? " (note: a trailing relationship column is appended)" : "");
            return false;
        }

        m_numRows = static_cast<uint32_t>(dec.ids.size());
        m_records = std::move(dec.records);
        m_ids     = std::move(dec.ids);
        m_strings = std::move(dec.strings);

        BuildIndex();
        m_loaded = true;
        WLOG_INFO("DB2: loaded %s (%u rows, id %d..%d)", nameForLog, m_numRows, m_minId, m_maxId);
        return true;
    }

    void DB2File::BuildIndex()
    {
        m_idIndex.clear();
        m_idIndex.reserve(m_ids.size());
        m_minId = 0x7FFFFFFF;
        m_maxId = -1;
        for (uint32_t i = 0; i < m_ids.size(); ++i)
        {
            int32_t id = m_ids[i];
            m_idIndex[id] = i;
            if (id < m_minId) m_minId = id;
            if (id > m_maxId) m_maxId = id;
        }
        if (m_ids.empty()) { m_minId = 0; m_maxId = -1; }
    }

    void DB2File::Unload()
    {
        m_loaded = false;
        m_numRows = 0;
        m_minId = 0x7FFFFFFF;
        m_maxId = -1;
        m_records.clear();
        m_ids.clear();
        m_strings.clear();
        m_idIndex.clear();
    }

    const void* DB2File::RowById(int32_t id) const
    {
        auto it = m_idIndex.find(id);
        if (it == m_idIndex.end()) return nullptr;
        return m_records.data() + static_cast<size_t>(it->second) * m_rowSize;
    }

    const void* DB2File::RowByIndex(uint32_t index) const
    {
        if (index >= m_numRows) return nullptr;
        return m_records.data() + static_cast<size_t>(index) * m_rowSize;
    }

    int32_t DB2File::IdAt(uint32_t index) const
    {
        return (index < m_ids.size()) ? m_ids[index] : -1;
    }

    const char* DB2File::Str(uint32_t offset) const
    {
        if (offset >= m_strings.size()) return "";
        return m_strings.data() + offset;
    }
}
