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
#include <vector>
#include <unordered_map>

// DB2File: one .db2 table loaded from the mounted archives and decoded into a flat array of fixed-size
// records, indexed by id. The on-disk record (which may be bitpacked, palletised, common-data backed and
// split across sections) is reconstructed by the decoder into a native-width layout (see DB2.hpp):
//   [id (uint32)] [each field in file order at its native width] [relationship id (uint32), if any]
// laid out with natural C alignment, so a Definition row struct mirrors the table fields exactly (a
// uint16 field stays uint16, a uint8 stays uint8, an array stays an array; strings are int32 offsets
// resolved through Str()).
//
// A "Definition" is just that row struct plus the typed wrapper DB2Table<Row>. The struct's first member
// is the id, then the fields in file order. The base validates that sizeof(Row) matches the decoded
// record size (so a layout mismatch is reported, not silently mis-read).
namespace wxl::features::db2
{
    // Native-DBC override target a Definition can declare so the stock client's reads of a built-in DBC
    // are served from this DB2 instead. Addresses are client-specific. The override install
    // (see DB2Mgr) reads these; fields not needed for a given table are left 0.
    struct NativeOverride
    {
        const char* dbcName;          // stock DBC being overridden, e.g. "CharSections.dbc"
        uintptr_t   globalObject;     // WowClientDB instance
        uintptr_t   recordCountField; // absolute address of the recordCount field
        uintptr_t   recordDataField;  // absolute address of the recordData pointer field
        uint32_t    nativeColumns;    // asserted native column count
        uint32_t    nativeRowSize;    // asserted native record size in bytes
        uintptr_t   lookupFn;         // consumer accessor rows are read through (the hook point)
        uintptr_t   cacheBuilderFn;   // builds the consumer cache from the records
        uintptr_t   cachePtr;         // consumer cache root
    };

    class DB2File
    {
    public:
        explicit DB2File(uint32_t rowSize);
        virtual ~DB2File() = default;

        // Load "DBFilesClient\<fileName>" from the mounted archives (uses the client storage API).
        // Client-only. Returns false (and logs) on any error; never throws or crashes the client.
        bool Load(const char* fileName);
        // Decode a .db2 already in memory (host-side disk read, or client). Arch-neutral, no storage API.
        bool LoadBytes(const uint8_t* data, uint32_t size, const char* nameForLog);
        void Unload();
        bool IsLoaded() const { return m_loaded; }

        int32_t  MinId()    const { return m_minId; }
        int32_t  MaxId()    const { return m_maxId; }
        uint32_t RowCount() const { return m_numRows; }

        // Decoded record for a given id (or row index), or nullptr if absent.
        const void* RowById(int32_t id) const;
        const void* RowByIndex(uint32_t index) const;
        // Id of a row by its index, or -1 if out of range.
        int32_t IdAt(uint32_t index) const;
        // Resolve a string-table offset to a C string ("" if out of range).
        const char* Str(uint32_t offset) const;

        // A Definition may override this to declare the stock DBC this table replaces and the native hook
        // points used to serve our data to the engine. Default: no override.
        virtual const NativeOverride* Override() const { return nullptr; }

    protected:
        // A Definition may override this to list the FIELD indices (file order, 0-based) that hold string
        // offsets. It is only needed for WDC2/WDC3 tables, or multi-section tables; a single-section WDC1
        // table stores absolute offsets and needs nothing here.
        virtual const uint32_t* StringColumns(uint32_t* count) const { *count = 0; return nullptr; }

        uint32_t m_rowSize;             // expected decoded record size (= sizeof(Row))
        bool     m_loaded = false;
        uint32_t m_numRows = 0;
        int32_t  m_minId = 0x7FFFFFFF;
        int32_t  m_maxId = -1;

        std::vector<char>    m_records; // m_numRows * m_rowSize decoded bytes
        std::vector<int32_t> m_ids;     // per-row id, parallel to m_records
        std::vector<char>    m_strings; // reconstructed string table
        std::unordered_map<int32_t, uint32_t> m_idIndex; // id -> row index

    private:
        void BuildIndex();              // fill m_idIndex + min/max from m_ids
    };

    // Typed Definition wrapper: a table of Row records keyed by id.
    template <typename Row>
    class DB2Table : public DB2File
    {
    public:
        DB2Table() : DB2File(sizeof(Row)) {}

        const Row* Find(int32_t id)   const { return static_cast<const Row*>(RowById(id)); }
        const Row* At(uint32_t index) const { return static_cast<const Row*>(RowByIndex(index)); }
    };
}
