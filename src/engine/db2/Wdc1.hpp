// Generic WDC1 reader over an in-memory byte buffer.
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

/**
 * @brief Minimal, dependency-free reader for the "plain fixed-record" WDC1 subset.
 *
 * The full format supports bitpacked fields, pallet /
 * common value stores, sparse offset maps, separate id lists, copy tables and relationship data.
 * The custom FileDataID tables this engine consumes (TextureFilePath / ModelFilePath) use none of
 * that: fixed-size records, the id stored inline at id_index, one string field whose value is a
 * byte offset into the trailing string table, records sorted ascending by id.
 *
 * This reader parses exactly that subset. It is PURE: it takes an owned byte buffer and never
 * touches the filesystem -- the caller reads the file (via the engine IO primitives) and hands the
 * bytes to Load(). Anything outside the subset (nonzero flags / id-list / pallet / common /
 * relationship sizes) is refused by Load() with a log, so a future DB2 that needs the richer format
 * fails loudly here instead of being silently misread; extend the reader when that day comes.
 *
 * Field access is generic (U32 / Str by field index); typed per-table views live in schema/*.hpp.
 */
namespace wxl::db2
{
    class Wdc1
    {
    public:
        Wdc1() = default;

        /// Parse @p bytes (ownership taken). Returns false (and logs) on a bad magic or any
        /// out-of-subset feature; the object is left empty (RecordCount()==0) on failure.
        bool Load(std::vector<uint8_t> bytes);

        bool     Ok() const { return ok_; }
        uint32_t RecordCount() const { return recordCount_; }
        uint32_t FieldCount() const { return fieldCount_; }

        /// The record's primary key (the field at id_index, read as u32).
        uint32_t Id(uint32_t rec) const;

        /// Raw 4-byte field @p field of record @p rec (0 if out of range).
        uint32_t U32(uint32_t rec, uint32_t field) const;

        /// NUL-terminated string for a string-typed field: the field holds an offset into the
        /// string table. Returns a pointer into the resident buffer (stable for this object's life),
        /// or "" when out of range.
        const char* Str(uint32_t rec, uint32_t field) const;

        /// Record index whose Id() == @p id via binary search (records are id-sorted ascending),
        /// or -1 when absent.
        int IndexOfId(uint32_t id) const;

    private:
        std::vector<uint8_t> data_;
        bool     ok_             = false;
        uint32_t recordCount_    = 0;
        uint32_t fieldCount_     = 0;
        uint32_t recordSize_     = 0;
        uint32_t stringTableSize_= 0;
        uint16_t idIndex_        = 0;
        size_t   recStart_       = 0;   // byte offset of the record block
        size_t   strStart_       = 0;   // byte offset of the string table
        // Per-field byte position within a record (from the field_structure section).
        std::vector<uint16_t> fieldPos_;

        uint32_t RdU32(size_t off) const;
    };
}
