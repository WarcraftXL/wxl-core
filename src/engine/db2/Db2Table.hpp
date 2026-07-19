// Generic typed view over a WDC1 table: plug a schema struct, resolve records by id.
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
 * @brief One typed table = one WDC1 file + one schema.
 *
 * A schema is a plain struct in db2/schema/*.hpp declaring the DB2's path (kFile), which fields it
 * cares about, and a static Read(db, rec) that lifts a record into typed fields. Db2Table wraps the
 * generic Wdc1 reader and exposes Find(id) -> schema. Adding a new DB2 is: write the schema header,
 * instantiate Db2Table<Schema>. Nothing else. The bytes are read by the caller (a feature that may
 * touch the engine IO primitives) and handed to Load().
 */
namespace wxl::db2
{
    template <class Schema>
    class Db2Table
    {
    public:
        /// Parse an already-read file image (ownership taken).
        bool Load(std::vector<uint8_t> bytes) { return db_.Load(std::move(bytes)); }

        bool     Ok() const { return db_.Ok(); }
        uint32_t RecordCount() const { return db_.RecordCount(); }
        const Wdc1& Raw() const { return db_; }

        /// Resolve @p id to a typed record. Returns false when the id is absent.
        bool Find(uint32_t id, Schema& out) const
        {
            const int i = db_.IndexOfId(id);
            if (i < 0)
                return false;
            out = Schema::Read(db_, static_cast<uint32_t>(i));
            return true;
        }

    private:
        Wdc1 db_;
    };
}
