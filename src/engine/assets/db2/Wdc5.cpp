// Minimal WDC5 reader used by the retail DB2 compatibility layer.
// Format notes: https://wowdev.wiki/DB2 (WDC5), mirrored locally by the project owner.
// Copyright (C) 2026 WarcraftXL. GPLv3.

#include "Wdc5.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <type_traits>
#include <utility>

namespace wxl::runtime::db2::wdc5
{
    namespace
    {
        constexpr uint32_t kMagic = 0x35434457u; // WDC5
        constexpr uint16_t kFlagSparse = 0x0001u;
        constexpr uint16_t kFlagSecondaryKey = 0x0002u;
        constexpr uint16_t kFlagNonInlineId = 0x0004u;

        enum class Compression : uint32_t
        {
            None = 0,
            Immediate = 1,
            Common = 2,
            Pallet = 3,
            PalletArray = 4,
            SignedImmediate = 5,
        };

#pragma pack(push, 1)
        struct Header
        {
            uint32_t magic;
            uint32_t schemaVersion;
            char schema[128];
            uint32_t recordCount;
            uint32_t fieldCount;
            uint32_t recordSize;
            uint32_t stringTableSize;
            uint32_t tableHash;
            uint32_t layoutHash;
            int32_t minId;
            int32_t maxId;
            uint32_t locale;
            uint16_t flags;
            uint16_t idIndex;
            uint32_t totalFieldCount;
            uint32_t bitpackedDataOffset;
            uint32_t lookupColumnCount;
            uint32_t fieldStorageInfoSize;
            uint32_t commonDataSize;
            uint32_t palletDataSize;
            uint32_t sectionCount;
        };

        struct SectionHeader
        {
            uint64_t tactKeyLookup;
            uint32_t fileOffset;
            uint32_t recordCount;
            uint32_t stringTableSize;
            uint32_t offsetRecordsEnd;
            uint32_t idListSize;
            uint32_t relationshipDataSize;
            uint32_t offsetMapIdCount;
            uint32_t copyTableCount;
        };

        struct FieldMeta
        {
            int16_t bits;
            uint16_t offset;
        };

        struct ColumnMeta
        {
            uint16_t recordOffsetBits;
            uint16_t sizeBits;
            uint32_t additionalDataSize;
            uint32_t compression;
            uint32_t value1;
            uint32_t value2;
            uint32_t value3;
        };

        struct SparseEntry
        {
            uint32_t offset;
            uint16_t size;
        };

        struct ReferenceEntry
        {
            uint32_t id;
            uint32_t index;
        };
#pragma pack(pop)

        // WDC5's fixed prefix is 204 bytes. Some readers use 200 as their minimum-size guard,
        // but section_count itself occupies bytes 200..203.
        static_assert(sizeof(Header) == 204);
        static_assert(sizeof(SectionHeader) == 40);
        static_assert(sizeof(FieldMeta) == 4);
        static_assert(sizeof(ColumnMeta) == 24);
        static_assert(sizeof(SparseEntry) == 6);
        static_assert(sizeof(ReferenceEntry) == 8);

        class Cursor
        {
        public:
            Cursor(const uint8_t* data, size_t size) : data_(data), size_(size) {}

            template <class T>
            bool Read(T& value)
            {
                static_assert(std::is_trivially_copyable_v<T>);
                if (pos_ > size_ || sizeof(T) > size_ - pos_) return false;
                std::memcpy(&value, data_ + pos_, sizeof(T));
                pos_ += sizeof(T);
                return true;
            }

            template <class T>
            bool ReadArray(std::vector<T>& out, size_t count)
            {
                if (count > (std::numeric_limits<size_t>::max)() / sizeof(T)) return false;
                const size_t bytes = count * sizeof(T);
                if (pos_ > size_ || bytes > size_ - pos_) return false;
                out.resize(count);
                if (bytes) std::memcpy(out.data(), data_ + pos_, bytes);
                pos_ += bytes;
                return true;
            }

            bool Seek(size_t pos)
            {
                if (pos > size_) return false;
                pos_ = pos;
                return true;
            }

            bool Skip(size_t bytes)
            {
                return bytes <= size_ - std::min(pos_, size_) && Seek(pos_ + bytes);
            }

            size_t Position() const noexcept { return pos_; }
            const uint8_t* Data() const noexcept { return data_; }
            size_t Size() const noexcept { return size_; }

        private:
            const uint8_t* data_ = nullptr;
            size_t size_ = 0;
            size_t pos_ = 0;
        };

        class BitReader
        {
        public:
            BitReader(const uint8_t* data, size_t bytes) : data_(data), bits_(bytes * 8) {}

            void SetBit(size_t bit) noexcept { bit_ = bit; }
            size_t Bit() const noexcept { return bit_; }

            bool Read(uint32_t count, uint64_t& out)
            {
                if (count > 64 || bit_ > bits_ || count > bits_ - bit_) return false;
                out = 0;
                for (uint32_t i = 0; i < count; ++i)
                {
                    const size_t source = bit_ + i;
                    out |= static_cast<uint64_t>((data_[source >> 3] >> (source & 7)) & 1u) << i;
                }
                bit_ += count;
                return true;
            }

        private:
            const uint8_t* data_ = nullptr;
            size_t bits_ = 0;
            size_t bit_ = 0;
        };

        using CommonMap = std::unordered_map<uint32_t, uint32_t>;

        bool Fail(std::string* error, const char* message)
        {
            if (error) *error = message;
            return false;
        }

        bool DecodeScalar(BitReader& bits, uint32_t rowId, const FieldMeta& field,
                          const ColumnMeta& column, const std::vector<uint32_t>& pallet,
                          const CommonMap& common, uint32_t& out)
        {
            uint64_t value = 0;
            const auto compression = static_cast<Compression>(column.compression);
            switch (compression)
            {
                case Compression::None:
                {
                    int width = 32 - field.bits;
                    if (width <= 0) width = static_cast<int>(column.value2);
                    if (width <= 0 || width > 32 || !bits.Read(static_cast<uint32_t>(width), value)) return false;
                    out = static_cast<uint32_t>(value);
                    return true;
                }
                case Compression::Immediate:
                    if (column.value2 > 32 || !bits.Read(column.value2, value)) return false;
                    out = static_cast<uint32_t>(value);
                    return true;
                case Compression::SignedImmediate:
                {
                    const uint32_t width = column.value2;
                    if (width == 0 || width > 32 || !bits.Read(width, value)) return false;
                    if (width < 32 && (value & (uint64_t{1} << (width - 1))))
                        value |= ~((uint64_t{1} << width) - 1);
                    out = static_cast<uint32_t>(value);
                    return true;
                }
                case Compression::Common:
                {
                    const auto it = common.find(rowId);
                    out = it == common.end() ? column.value1 : it->second;
                    return true;
                }
                case Compression::Pallet:
                case Compression::PalletArray:
                    if (column.value2 > 32 || !bits.Read(column.value2, value) || value >= pallet.size()) return false;
                    out = pallet[static_cast<size_t>(value)];
                    return true;
            }
            return false;
        }
    }

    bool Table::Load(const void* bytes, size_t size, const std::vector<FieldShape>& shapes,
                     uint32_t expectedLayoutHash, std::string* error)
    {
        rows_.clear();
        byId_.clear();
        fieldOffsets_.clear();
        layoutHash_ = 0;
        tableHash_ = 0;
        schema_.clear();
        if (error) error->clear();
        if (!bytes || size < sizeof(Header)) return Fail(error, "WDC5: file is smaller than its header");

        Cursor cursor(static_cast<const uint8_t*>(bytes), size);
        Header header{};
        if (!cursor.Read(header) || header.magic != kMagic) return Fail(error, "WDC5: invalid magic");
        if (expectedLayoutHash && header.layoutHash != expectedLayoutHash)
            return Fail(error, "WDC5: layout hash does not match the compiled schema");
        if (header.fieldCount == 0 || shapes.size() != header.fieldCount)
            return Fail(error, "WDC5: physical field count does not match the compiled schema");
        if (header.sectionCount == 0 && header.recordCount != 0)
            return Fail(error, "WDC5: records exist but the section table is empty");
        if ((header.flags & kFlagSparse) != 0)
            return Fail(error, "WDC5: sparse tables are not supported by this compatibility layer");

        fieldOffsets_.resize(header.fieldCount + 1);
        for (size_t i = 0; i < header.fieldCount; ++i)
            fieldOffsets_[i + 1] = fieldOffsets_[i] + std::max<uint16_t>(1, shapes[i].elements);

        layoutHash_ = header.layoutHash;
        tableHash_ = header.tableHash;
        schema_.assign(header.schema, header.schema + sizeof(header.schema));
        schema_.erase(std::find(schema_.begin(), schema_.end(), '\0'), schema_.end());

        std::vector<SectionHeader> sections;
        std::vector<FieldMeta> fieldMeta;
        std::vector<ColumnMeta> columnMeta;
        if (!cursor.ReadArray(sections, header.sectionCount) ||
            !cursor.ReadArray(fieldMeta, header.fieldCount) ||
            !cursor.ReadArray(columnMeta, header.fieldCount))
            return Fail(error, "WDC5: truncated metadata");

        std::vector<std::vector<uint32_t>> pallets(header.fieldCount);
        std::vector<CommonMap> commons(header.fieldCount);
        for (size_t i = 0; i < columnMeta.size(); ++i)
        {
            const auto compression = static_cast<Compression>(columnMeta[i].compression);
            if (compression == Compression::Pallet || compression == Compression::PalletArray)
            {
                if ((columnMeta[i].additionalDataSize & 3u) != 0 ||
                    !cursor.ReadArray(pallets[i], columnMeta[i].additionalDataSize / 4u))
                    return Fail(error, "WDC5: truncated pallet data");
            }
        }
        for (size_t i = 0; i < columnMeta.size(); ++i)
        {
            if (static_cast<Compression>(columnMeta[i].compression) != Compression::Common) continue;
            if ((columnMeta[i].additionalDataSize & 7u) != 0)
                return Fail(error, "WDC5: malformed common-data size");
            const size_t count = columnMeta[i].additionalDataSize / 8u;
            for (size_t j = 0; j < count; ++j)
            {
                uint32_t id = 0, value = 0;
                if (!cursor.Read(id) || !cursor.Read(value)) return Fail(error, "WDC5: truncated common data");
                commons[i][id] = value;
            }
        }

        // Encrypted-id lists are stored between column auxiliary data and the first section.
        for (const SectionHeader& section : sections)
        {
            if (section.tactKeyLookup == 0) continue;
            uint32_t count = 0;
            if (!cursor.Read(count) || !cursor.Skip(static_cast<size_t>(count) * sizeof(uint32_t)))
                return Fail(error, "WDC5: truncated encrypted-id list");
        }

        rows_.reserve(header.recordCount);
        std::unordered_map<uint32_t, uint32_t> copies;
        uint32_t globalRecordIndex = 0;
        for (const SectionHeader& section : sections)
        {
            if (!cursor.Seek(section.fileOffset)) return Fail(error, "WDC5: section offset is outside the file");
            const size_t recordBytes = static_cast<size_t>(section.recordCount) * header.recordSize;
            if (header.recordSize && recordBytes / header.recordSize != section.recordCount)
                return Fail(error, "WDC5: record byte count overflow");
            if (cursor.Position() > size || recordBytes > size - cursor.Position())
                return Fail(error, "WDC5: truncated record block");
            const uint8_t* recordData = cursor.Data() + cursor.Position();
            if (!cursor.Skip(recordBytes) || !cursor.Skip(section.stringTableSize))
                return Fail(error, "WDC5: truncated record/string block");

            std::vector<uint32_t> ids;
            if ((section.idListSize & 3u) != 0 || !cursor.ReadArray(ids, section.idListSize / 4u))
                return Fail(error, "WDC5: truncated id list");
            if (!ids.empty() && ids.size() != section.recordCount)
                return Fail(error, "WDC5: id-list length does not match section records");

            for (uint32_t i = 0; i < section.copyTableCount; ++i)
            {
                uint32_t destination = 0, source = 0;
                if (!cursor.Read(destination) || !cursor.Read(source))
                    return Fail(error, "WDC5: truncated copy table");
                if (destination != source) copies[destination] = source;
            }

            if (section.offsetMapIdCount != 0)
                return Fail(error, "WDC5: unexpected offset map in a non-sparse retail table");

            std::unordered_map<uint32_t, uint32_t> parentByIndex;
            if (section.relationshipDataSize)
            {
                const size_t relationshipStart = cursor.Position();
                uint32_t count = 0, minId = 0, maxId = 0;
                if (!cursor.Read(count) || !cursor.Read(minId) || !cursor.Read(maxId))
                    return Fail(error, "WDC5: truncated relationship header");
                (void)minId;
                (void)maxId;
                for (uint32_t i = 0; i < count; ++i)
                {
                    ReferenceEntry entry{};
                    if (!cursor.Read(entry)) return Fail(error, "WDC5: truncated relationship data");
                    parentByIndex[entry.index] = entry.id;
                }
                const size_t consumed = cursor.Position() - relationshipStart;
                if (consumed > section.relationshipDataSize ||
                    !cursor.Skip(section.relationshipDataSize - consumed))
                    return Fail(error, "WDC5: malformed relationship-data size");
            }

            for (uint32_t i = 0; i < section.recordCount; ++i, ++globalRecordIndex)
            {
                Row row;
                row.values.resize(fieldOffsets_.back());
                BitReader bits(recordData + static_cast<size_t>(i) * header.recordSize, header.recordSize);

                if ((header.flags & kFlagNonInlineId) != 0)
                {
                    if (ids.empty()) return Fail(error, "WDC5: non-inline ID table has no id list");
                    row.id = ids[i];
                }

                for (size_t fieldIndex = 0; fieldIndex < header.fieldCount; ++fieldIndex)
                {
                    const ColumnMeta& column = columnMeta[fieldIndex];
                    const auto compression = static_cast<Compression>(column.compression);
                    uint16_t elements = std::max<uint16_t>(1, shapes[fieldIndex].elements);
                    if (compression == Compression::PalletArray)
                        elements = static_cast<uint16_t>(column.value3);

                    // The compiled shape and WDC5 pallet cardinality should describe the same logical array.
                    if (fieldOffsets_[fieldIndex + 1] - fieldOffsets_[fieldIndex] != elements)
                        return Fail(error, "WDC5: pallet cardinality does not match the compiled schema");
                    uint32_t* values = row.values.data() + fieldOffsets_[fieldIndex];
                    if (compression == Compression::PalletArray)
                    {
                        uint64_t palletIndex = 0;
                        if (column.value2 > 32 || !bits.Read(column.value2, palletIndex))
                            return Fail(error, "WDC5: invalid pallet-array index");
                        const size_t first = static_cast<size_t>(palletIndex) * elements;
                        if (first > pallets[fieldIndex].size() || elements > pallets[fieldIndex].size() - first)
                            return Fail(error, "WDC5: pallet-array index is outside its pallet");
                        std::copy_n(pallets[fieldIndex].begin() + first, elements, values);
                    }
                    else
                    {
                        for (uint16_t element = 0; element < elements; ++element)
                        {
                            if (!DecodeScalar(bits, row.id, fieldMeta[fieldIndex], column,
                                              pallets[fieldIndex], commons[fieldIndex], values[element]))
                                return Fail(error, "WDC5: failed to decode a field");
                        }
                    }

                    if ((header.flags & kFlagNonInlineId) == 0 && fieldIndex == header.idIndex)
                        row.id = elements == 0 ? 0 : values[0];
                }

                const uint32_t relationshipKey = (header.flags & kFlagSecondaryKey) != 0 ? row.id : i;
                const auto parent = parentByIndex.find(relationshipKey);
                if (parent != parentByIndex.end()) row.parentId = parent->second;
                rows_.push_back(std::move(row));
            }
        }

        for (const auto& [destination, source] : copies)
        {
            const auto it = std::find_if(rows_.begin(), rows_.end(), [source](const Row& row) { return row.id == source; });
            if (it == rows_.end()) continue;
            Row copy = *it;
            copy.id = destination;
            rows_.push_back(std::move(copy));
        }

        byId_.reserve(rows_.size());
        for (size_t i = 0; i < rows_.size(); ++i) byId_[rows_[i].id] = i;
        return true;
    }

    const Row* Table::Find(uint32_t id) const noexcept
    {
        const auto it = byId_.find(id);
        return it == byId_.end() ? nullptr : &rows_[it->second];
    }

    uint32_t Table::Value(const Row& row, size_t field, size_t element) const noexcept
    {
        if (field + 1 >= fieldOffsets_.size()) return 0;
        const size_t begin = fieldOffsets_[field];
        const size_t end = fieldOffsets_[field + 1];
        return element < end - begin && begin + element < row.values.size() ? row.values[begin + element] : 0;
    }

    size_t Table::ElementCount(size_t field) const noexcept
    {
        return field + 1 < fieldOffsets_.size() ? fieldOffsets_[field + 1] - fieldOffsets_[field] : 0;
    }
}
