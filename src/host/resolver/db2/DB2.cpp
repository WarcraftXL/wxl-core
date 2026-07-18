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
#include "DB2.hpp"

#include "common/Log.hpp"

#include <cstring>
#include <unordered_map>

using namespace wxl;

namespace
{
    constexpr uint32_t kMagicWDC1 = 0x31434457; // 'WDC1'
    constexpr uint32_t kMagicWDC2 = 0x32434457; // 'WDC2'
    constexpr uint32_t kMagic1SLC = 0x434C5331; // '1SLC'
    constexpr uint32_t kMagicWDC3 = 0x33434457; // 'WDC3'

#pragma pack(push, 1)
    struct FieldStructure
    {
        int16_t  size;       // byteSize = (32 - size) / 8; negative size => field larger than 32 bits
        uint16_t position;   // byte offset of the field within the record
    };

    struct FieldStorageInfo
    {
        uint16_t fieldOffsetBits;
        uint16_t fieldSizeBits;      // total bits of the field (arrays summed)
        uint32_t additionalDataSize; // bytes this field occupies in pallet_data or common_data
        uint32_t storageType;        // field_compression enum
        uint32_t p1, p2, p3;         // type-specific (default_value for common, array_count for type 4, ...)
    };

    struct Wdc1Header
    {
        uint32_t magic, recordCount, fieldCount, recordSize, stringTableSize;
        uint32_t tableHash, layoutHash, minId, maxId, locale, copyTableSize;
        uint16_t flags, idIndex;
        uint32_t totalFieldCount, bitpackedDataOffset, lookupColumnCount, offsetMapOffset;
        uint32_t idListSize, fieldStorageInfoSize, commonDataSize, palletDataSize, relationshipDataSize;
    };

    struct Wdc23Header
    {
        uint32_t magic, recordCount, fieldCount, recordSize, stringTableSize;
        uint32_t tableHash, layoutHash, minId, maxId, locale;
        uint16_t flags, idIndex;
        uint32_t totalFieldCount, bitpackedDataOffset, lookupColumnCount;
        uint32_t fieldStorageInfoSize, commonDataSize, palletDataSize, sectionCount;
    };

    struct Wdc2Section
    {
        uint64_t tactKeyHash;
        uint32_t fileOffset, recordCount, stringTableSize, copyTableSize;
        uint32_t offsetMapOffset, idListSize, relationshipDataSize;
    };

    struct Wdc3Section
    {
        uint64_t tactKeyHash;
        uint32_t fileOffset, recordCount, stringTableSize, offsetRecordsEnd;
        uint32_t idListSize, relationshipDataSize, offsetMapIdCount, copyTableCount;
    };
#pragma pack(pop)

    // Little-endian bitfield read of up to ~57 useful bits (enough for every DB2 bitpacked field).
    uint64_t ReadBitsU(const uint8_t* p, uint32_t bitOffset, uint32_t bitCount)
    {
        if (bitCount == 0) return 0;
        uint32_t byteIdx = bitOffset >> 3;
        uint32_t shift   = bitOffset & 7;
        uint32_t need    = (bitCount + shift + 7) >> 3;
        uint64_t v = 0;
        for (uint32_t i = 0; i < need && i < 8; ++i)
            v |= static_cast<uint64_t>(p[byteIdx + i]) << (8 * i);
        v >>= shift;
        if (bitCount < 64) v &= (static_cast<uint64_t>(1) << bitCount) - 1;
        return v;
    }

    int32_t SignExtend(uint64_t v, uint32_t bits)
    {
        if (bits == 0 || bits >= 32) return static_cast<int32_t>(v);
        uint64_t m = static_cast<uint64_t>(1) << (bits - 1);
        return static_cast<int32_t>((v ^ m) - m);
    }

    // One section's assembled blocks (pointers into the file buffer).
    struct Section
    {
        const uint8_t* records = nullptr;
        uint32_t recordCount = 0;
        uint32_t recordsFileOffset = 0;
        uint32_t stringFileOffset = 0;
        uint32_t stringSize = 0;
        const uint32_t* idList = nullptr;
        uint32_t idListCount = 0;
        std::vector<std::pair<uint32_t, uint32_t>> copyTable;          // (newId, srcId)
        std::vector<std::pair<uint32_t, uint16_t>> offsetMap;          // (fileOffset, size) for flags&1
        std::vector<uint32_t> offsetMapIds;                            // explicit ids (WDC3); empty => implicit
        std::unordered_map<uint32_t, uint32_t> relationship;           // recordIndex -> foreignId
        uint32_t stringBaseInBlob = 0;                                 // base of this section's strings in out.strings
    };

    // Per-field decode plan, precomputed once. The decoder reconstructs each record into a native-width
    // layout (a uint16 stays 2 bytes, a uint8 stays 1 byte, an array stays consecutive), matching a
    // naturally-packed C struct, so a Definition row mirrors the table fields exactly.
    struct FieldMeta
    {
        int storageType = 0;
        uint32_t fieldOffsetBits = 0;
        uint32_t fieldSizeBits = 0;
        uint32_t elementBytes = 4;      // native width of one element (1/2/4/8)
        uint32_t elementCount = 1;      // array length
        uint32_t palletBase = 0;        // byte base into palletData (type 3/4)
        int32_t  defaultValue = 0;      // type 2
        bool     signExt = false;       // type 1 signed
        uint32_t outByteOffset = 0;     // byte offset of this field in the decoded record
        bool     isInlineId = false;    // this field carries the id (skip emitting; id slot holds it)
        std::unordered_map<uint32_t, uint32_t> common; // type 2 id -> value
    };

    struct Parsed
    {
        uint32_t version = 0;           // 1, 2, 3
        uint32_t recordSize = 0;
        uint32_t fieldCount = 0;
        uint32_t totalFieldCount = 0;
        uint16_t flags = 0;
        uint16_t idIndex = 0;
        int32_t  minId = 0;
        int32_t  maxId = 0;
        const FieldStructure*   fieldStruct = nullptr;
        const FieldStorageInfo* fieldInfo = nullptr;
        uint32_t fieldInfoCount = 0;
        bool     hasIdList = false;      // true => id comes from a section id list (not inline)
        const uint8_t* palletData = nullptr;
        const uint8_t* commonData = nullptr;
        std::vector<Section> sections;
    };

    bool ParseWDC1(const uint8_t* d, uint32_t size, Parsed& out)
    {
        if (size < sizeof(Wdc1Header)) return false;
        const auto& h = *reinterpret_cast<const Wdc1Header*>(d);

        out.version = 1;
        out.recordSize = h.recordSize;
        out.fieldCount = h.fieldCount;
        out.totalFieldCount = h.totalFieldCount;
        out.flags = h.flags;
        out.idIndex = h.idIndex;
        out.minId = static_cast<int32_t>(h.minId);
        out.maxId = static_cast<int32_t>(h.maxId);

        uint32_t cur = sizeof(Wdc1Header);
        out.fieldStruct = reinterpret_cast<const FieldStructure*>(d + cur);
        cur += h.totalFieldCount * sizeof(FieldStructure);

        Section sec;
        const bool offsetMap = (h.flags & 0x01) != 0;
        if (!offsetMap)
        {
            sec.records = d + cur;
            sec.recordCount = h.recordCount;
            sec.recordsFileOffset = cur;
            cur += h.recordCount * h.recordSize;
            sec.stringFileOffset = cur;
            sec.stringSize = h.stringTableSize;
            cur += h.stringTableSize;
        }
        else
        {
            // Variable-length records pointed at by an offset map after the strings region.
            cur = h.offsetMapOffset;
            uint32_t entries = (h.maxId >= h.minId) ? (h.maxId - h.minId + 1) : 0;
            for (uint32_t i = 0; i < entries; ++i)
            {
                uint32_t off = *reinterpret_cast<const uint32_t*>(d + cur); cur += 4;
                uint16_t len = *reinterpret_cast<const uint16_t*>(d + cur); cur += 2;
                if (off != 0 && len != 0) sec.offsetMap.emplace_back(off, len);
            }
            sec.recordCount = static_cast<uint32_t>(sec.offsetMap.size());
        }

        if (h.idListSize)
        {
            sec.idList = reinterpret_cast<const uint32_t*>(d + cur);
            sec.idListCount = h.idListSize / 4;
            cur += h.idListSize;
            out.hasIdList = true;
        }
        if (h.copyTableSize)
        {
            uint32_t n = h.copyTableSize / 8;
            const uint32_t* ct = reinterpret_cast<const uint32_t*>(d + cur);
            for (uint32_t i = 0; i < n; ++i) sec.copyTable.emplace_back(ct[2 * i], ct[2 * i + 1]);
            cur += h.copyTableSize;
        }

        out.fieldInfo = reinterpret_cast<const FieldStorageInfo*>(d + cur);
        out.fieldInfoCount = h.fieldStorageInfoSize / sizeof(FieldStorageInfo);
        cur += h.fieldStorageInfoSize;
        out.palletData = d + cur; cur += h.palletDataSize;
        out.commonData = d + cur; cur += h.commonDataSize;

        if (h.relationshipDataSize)
        {
            uint32_t numEntries = *reinterpret_cast<const uint32_t*>(d + cur);
            const uint32_t* e = reinterpret_cast<const uint32_t*>(d + cur + 12);
            for (uint32_t i = 0; i < numEntries; ++i)
                sec.relationship[e[2 * i + 1]] = e[2 * i]; // recordIndex -> foreignId
            cur += h.relationshipDataSize;
        }

        out.sections.push_back(std::move(sec));
        return true;
    }

    bool ParseWDC23(const uint8_t* d, uint32_t size, Parsed& out, uint32_t version)
    {
        if (size < sizeof(Wdc23Header)) return false;
        const auto& h = *reinterpret_cast<const Wdc23Header*>(d);

        out.version = version;
        out.recordSize = h.recordSize;
        out.fieldCount = h.fieldCount;
        out.totalFieldCount = h.totalFieldCount;
        out.flags = h.flags;
        out.idIndex = h.idIndex;
        out.minId = static_cast<int32_t>(h.minId);
        out.maxId = static_cast<int32_t>(h.maxId);

        uint32_t cur = sizeof(Wdc23Header);
        const uint8_t* sectionHeaders = d + cur;
        cur += h.sectionCount * (version == 3 ? sizeof(Wdc3Section) : sizeof(Wdc2Section));

        out.fieldStruct = reinterpret_cast<const FieldStructure*>(d + cur);
        cur += h.totalFieldCount * sizeof(FieldStructure);
        out.fieldInfo = reinterpret_cast<const FieldStorageInfo*>(d + cur);
        out.fieldInfoCount = h.fieldStorageInfoSize / sizeof(FieldStorageInfo);
        cur += h.fieldStorageInfoSize;
        out.palletData = d + cur; cur += h.palletDataSize;
        out.commonData = d + cur; cur += h.commonDataSize;

        const bool offsetMap = (h.flags & 0x01) != 0;

        for (uint32_t s = 0; s < h.sectionCount; ++s)
        {
            Section sec;
            if (version == 3)
            {
                const auto& sh = reinterpret_cast<const Wdc3Section*>(sectionHeaders)[s];
                uint32_t p = sh.fileOffset;
                if (!offsetMap)
                {
                    sec.records = d + p;
                    sec.recordCount = sh.recordCount;
                    sec.recordsFileOffset = p;
                    p += sh.recordCount * h.recordSize;
                    sec.stringFileOffset = p;
                    sec.stringSize = sh.stringTableSize;
                    p += sh.stringTableSize;
                }
                else
                {
                    sec.recordsFileOffset = p;
                    p = sh.offsetRecordsEnd;
                }
                if (sh.idListSize) { sec.idList = reinterpret_cast<const uint32_t*>(d + p); sec.idListCount = sh.idListSize / 4; p += sh.idListSize; out.hasIdList = true; }
                if (sh.copyTableCount)
                {
                    const uint32_t* ct = reinterpret_cast<const uint32_t*>(d + p);
                    for (uint32_t i = 0; i < sh.copyTableCount; ++i) sec.copyTable.emplace_back(ct[2 * i], ct[2 * i + 1]);
                    p += sh.copyTableCount * 8;
                }
                if (offsetMap)
                {
                    const uint8_t* om = d + p;
                    const uint32_t* ids = reinterpret_cast<const uint32_t*>(om + sh.offsetMapIdCount * 6);
                    for (uint32_t i = 0; i < sh.offsetMapIdCount; ++i)
                    {
                        uint32_t off = *reinterpret_cast<const uint32_t*>(om + i * 6);
                        uint16_t len = *reinterpret_cast<const uint16_t*>(om + i * 6 + 4);
                        sec.offsetMap.emplace_back(off, len);
                        sec.offsetMapIds.push_back(ids[i]);
                    }
                    sec.recordCount = sh.offsetMapIdCount;
                    p += sh.offsetMapIdCount * 6 + sh.offsetMapIdCount * 4;
                }
                if (sh.relationshipDataSize)
                {
                    uint32_t numEntries = *reinterpret_cast<const uint32_t*>(d + p);
                    const uint32_t* e = reinterpret_cast<const uint32_t*>(d + p + 12);
                    for (uint32_t i = 0; i < numEntries; ++i) sec.relationship[e[2 * i + 1]] = e[2 * i];
                    p += sh.relationshipDataSize;
                }
            }
            else // WDC2
            {
                const auto& sh = reinterpret_cast<const Wdc2Section*>(sectionHeaders)[s];
                uint32_t p = sh.fileOffset;
                if (!offsetMap)
                {
                    sec.records = d + p;
                    sec.recordCount = sh.recordCount;
                    sec.recordsFileOffset = p;
                    p += sh.recordCount * h.recordSize;
                    sec.stringFileOffset = p;
                    sec.stringSize = sh.stringTableSize;
                    p += sh.stringTableSize;
                }
                else
                {
                    sec.recordsFileOffset = p;
                    uint32_t entries = (h.maxId >= h.minId) ? (h.maxId - h.minId + 1) : 0;
                    uint32_t om = sh.offsetMapOffset;
                    for (uint32_t i = 0; i < entries; ++i)
                    {
                        uint32_t off = *reinterpret_cast<const uint32_t*>(d + om); om += 4;
                        uint16_t len = *reinterpret_cast<const uint16_t*>(d + om); om += 2;
                        if (off != 0 && len != 0) sec.offsetMap.emplace_back(off, len);
                    }
                    sec.recordCount = static_cast<uint32_t>(sec.offsetMap.size());
                    p = om;
                }
                if (sh.idListSize) { sec.idList = reinterpret_cast<const uint32_t*>(d + p); sec.idListCount = sh.idListSize / 4; p += sh.idListSize; out.hasIdList = true; }
                if (sh.copyTableSize)
                {
                    uint32_t n = sh.copyTableSize / 8;
                    const uint32_t* ct = reinterpret_cast<const uint32_t*>(d + p);
                    for (uint32_t i = 0; i < n; ++i) sec.copyTable.emplace_back(ct[2 * i], ct[2 * i + 1]);
                    p += sh.copyTableSize;
                }
                if (sh.relationshipDataSize)
                {
                    uint32_t numEntries = *reinterpret_cast<const uint32_t*>(d + p);
                    const uint32_t* e = reinterpret_cast<const uint32_t*>(d + p + 12);
                    for (uint32_t i = 0; i < numEntries; ++i) sec.relationship[e[2 * i + 1]] = e[2 * i];
                    p += sh.relationshipDataSize;
                }
            }
            out.sections.push_back(std::move(sec));
        }
        return true;
    }

    inline uint32_t AlignUp(uint32_t o, uint32_t a) { return (o + a - 1) & ~(a - 1); }

    // Round a raw on-disk field width to a width a C struct member can have (1/2/4/8).
    inline uint32_t StdWidth(uint32_t bytes)
    {
        if (bytes <= 1) return 1;
        if (bytes == 2) return 2;
        if (bytes <= 4) return 4;
        return 8;
    }

    // Native decoded-record layout, computed once. The record is [id u32][fields in file order at native
    // widths][relationship u32], laid out with natural C alignment so a Definition struct matches it.
    struct Layout
    {
        uint32_t rowSize = 0;
        uint32_t idOffset = 0;
        uint32_t relOffset = 0;
        bool     hasRel = false;
    };

    Layout BuildLayout(const Parsed& p, std::vector<FieldMeta>& meta, bool hasRel)
    {
        meta.resize(p.fieldInfoCount);
        const bool inlineId = !p.hasIdList;

        uint32_t palletCursor = 0, commonCursor = 0;
        Layout lay;
        lay.hasRel = hasRel;

        uint32_t o = 0, maxAlign = 4;
        lay.idOffset = o; o += 4; // id is always the first u32

        for (uint32_t i = 0; i < p.fieldInfoCount; ++i)
        {
            const FieldStorageInfo& fi = p.fieldInfo[i];
            FieldMeta& m = meta[i];
            m.storageType = static_cast<int>(fi.storageType);
            m.fieldOffsetBits = fi.fieldOffsetBits;
            m.fieldSizeBits = fi.fieldSizeBits;

            // On-disk element width from the field-structure block (logical width of one element).
            uint32_t diskBytes = 4;
            if (i < p.totalFieldCount && p.fieldStruct)
            {
                int16_t sz = p.fieldStruct[i].size;
                diskBytes = static_cast<uint32_t>((32 - sz) / 8);
                if (diskBytes == 0) diskBytes = 4;
            }
            m.elementBytes = StdWidth(diskBytes);

            switch (fi.storageType)
            {
                case 0: // none (inline): element width = diskBytes, count from total bits
                {
                    uint32_t elemBits = diskBytes * 8;
                    uint32_t totalBits = fi.fieldSizeBits ? fi.fieldSizeBits : elemBits;
                    m.elementCount = elemBits ? (totalBits / elemBits) : 1;
                    if (m.elementCount == 0) m.elementCount = 1;
                    break;
                }
                case 2: // common data
                {
                    m.elementCount = 1;
                    m.defaultValue = static_cast<int32_t>(fi.p1);
                    uint32_t n = fi.additionalDataSize / 8;
                    const uint32_t* cd = reinterpret_cast<const uint32_t*>(p.commonData + commonCursor);
                    for (uint32_t k = 0; k < n; ++k) m.common[cd[2 * k]] = cd[2 * k + 1];
                    commonCursor += fi.additionalDataSize;
                    break;
                }
                case 1: // bitpacked
                    m.elementCount = 1;
                    m.signExt = (fi.p3 & 0x01) != 0;
                    break;
                case 3: // bitpacked indexed
                    m.elementCount = 1;
                    m.palletBase = palletCursor;
                    palletCursor += fi.additionalDataSize;
                    break;
                case 4: // bitpacked indexed array
                    m.elementCount = fi.p3 ? fi.p3 : 1;
                    m.palletBase = palletCursor;
                    palletCursor += fi.additionalDataSize;
                    break;
                default:
                    m.elementCount = 1;
                    break;
            }

            // The inline id field is represented by the leading id slot, not emitted again.
            if (inlineId && i == p.idIndex)
            {
                m.isInlineId = true;
                continue;
            }

            if (m.elementBytes > maxAlign) maxAlign = m.elementBytes;
            o = AlignUp(o, m.elementBytes);
            m.outByteOffset = o;
            o += m.elementBytes * m.elementCount;
        }

        if (hasRel)
        {
            o = AlignUp(o, 4);
            lay.relOffset = o;
            o += 4;
        }
        lay.rowSize = AlignUp(o, maxAlign);
        return lay;
    }

    inline void WriteVal(char* dst, uint32_t width, uint64_t v)
    {
        for (uint32_t b = 0; b < width && b < 8; ++b) dst[b] = static_cast<char>((v >> (8 * b)) & 0xFF);
    }

    // Decode one field of one record and write its native-width value(s) into recOut.
    void DecodeField(const FieldMeta& m, const Parsed& p, const uint8_t* rec, int32_t id, char* recOut)
    {
        if (m.isInlineId) return;
        char* dst = recOut + m.outByteOffset;
        switch (m.storageType)
        {
            case 0:
            {
                uint32_t diskBytes = (m.fieldSizeBits && m.elementCount) ? (m.fieldSizeBits / 8 / m.elementCount) : m.elementBytes;
                if (diskBytes == 0) diskBytes = m.elementBytes;
                uint32_t baseByte = m.fieldOffsetBits / 8;
                for (uint32_t e = 0; e < m.elementCount; ++e)
                {
                    uint64_t v = 0;
                    const uint8_t* src = rec + baseByte + e * diskBytes;
                    for (uint32_t b = 0; b < diskBytes && b < 8; ++b) v |= static_cast<uint64_t>(src[b]) << (8 * b);
                    WriteVal(dst + e * m.elementBytes, m.elementBytes, v);
                }
                break;
            }
            case 1:
            {
                uint64_t v = ReadBitsU(rec, m.fieldOffsetBits, m.fieldSizeBits);
                int32_t iv = m.signExt ? SignExtend(v, m.fieldSizeBits) : static_cast<int32_t>(v);
                WriteVal(dst, m.elementBytes, static_cast<uint32_t>(iv));
                break;
            }
            case 2:
            {
                auto it = m.common.find(static_cast<uint32_t>(id));
                int32_t iv = (it != m.common.end()) ? static_cast<int32_t>(it->second) : m.defaultValue;
                WriteVal(dst, m.elementBytes, static_cast<uint32_t>(iv));
                break;
            }
            case 3:
            {
                uint64_t idx = ReadBitsU(rec, m.fieldOffsetBits, m.fieldSizeBits);
                const uint32_t* pal = reinterpret_cast<const uint32_t*>(p.palletData + m.palletBase);
                WriteVal(dst, m.elementBytes, pal[idx]);
                break;
            }
            case 4:
            {
                uint64_t idx = ReadBitsU(rec, m.fieldOffsetBits, m.fieldSizeBits);
                const uint32_t* pal = reinterpret_cast<const uint32_t*>(p.palletData + m.palletBase);
                for (uint32_t a = 0; a < m.elementCount; ++a)
                    WriteVal(dst + a * m.elementBytes, m.elementBytes, pal[idx * m.elementCount + a]);
                break;
            }
            default:
                break;
        }
    }

    // Read the scalar value of one field (used to resolve an inline id).
    int32_t ReadScalar(const FieldMeta& m, const Parsed& p, const uint8_t* rec)
    {
        switch (m.storageType)
        {
            case 0:
            {
                uint32_t diskBytes = (m.fieldSizeBits) ? (m.fieldSizeBits / 8) : m.elementBytes;
                if (diskBytes == 0 || diskBytes > 8) diskBytes = 4;
                uint64_t v = 0;
                const uint8_t* src = rec + m.fieldOffsetBits / 8;
                for (uint32_t b = 0; b < diskBytes && b < 8; ++b) v |= static_cast<uint64_t>(src[b]) << (8 * b);
                return static_cast<int32_t>(v);
            }
            case 1:
                return static_cast<int32_t>(ReadBitsU(rec, m.fieldOffsetBits, m.fieldSizeBits));
            case 3:
            {
                uint64_t idx = ReadBitsU(rec, m.fieldOffsetBits, m.fieldSizeBits);
                return static_cast<int32_t>(reinterpret_cast<const uint32_t*>(p.palletData + m.palletBase)[idx]);
            }
            default:
                return 0;
        }
    }

    // Resolve a record's id: from the section id list, else from the inline id field (idIndex).
    int32_t ResolveId(const Parsed& p, const std::vector<FieldMeta>& meta, const Section& sec, const uint8_t* rec,
                      uint32_t recordIndex)
    {
        if (sec.idList && recordIndex < sec.idListCount)
            return static_cast<int32_t>(sec.idList[recordIndex]);
        if (p.idIndex < meta.size())
            return ReadScalar(meta[p.idIndex], p, rec);
        return static_cast<int32_t>(recordIndex);
    }
}

namespace wxl::features::db2
{
    bool DecodeDB2(const uint8_t* data, uint32_t size, DB2Decoded& out, const uint32_t* stringColumns,
                   uint32_t stringColumnCount)
    {
        out = DB2Decoded{};
        if (size < 4) return false;

        uint32_t magic = *reinterpret_cast<const uint32_t*>(data);
        Parsed p;
        bool ok = false;
        if (magic == kMagicWDC1) ok = ParseWDC1(data, size, p);
        else if (magic == kMagicWDC2 || magic == kMagic1SLC) ok = ParseWDC23(data, size, p, 2);
        else if (magic == kMagicWDC3) ok = ParseWDC23(data, size, p, 3);
        else return false;
        if (!ok) return false;

        std::vector<FieldMeta> meta;
        bool hasRel = false;
        for (const auto& s : p.sections) if (!s.relationship.empty()) hasRel = true;
        out.hasRelationship = hasRel;

        Layout lay = BuildLayout(p, meta, hasRel);
        out.rowSize = lay.rowSize;

        // Reconstruct one contiguous string table from every section.
        for (auto& s : p.sections)
        {
            s.stringBaseInBlob = static_cast<uint32_t>(out.strings.size());
            if (s.stringSize)
            {
                const char* st = reinterpret_cast<const char*>(data + s.stringFileOffset);
                out.strings.insert(out.strings.end(), st, st + s.stringSize);
            }
        }
        if (out.strings.empty()) out.strings.push_back('\0');

        const bool multiSection = p.sections.size() > 1;

        // Decode every section's records.
        std::unordered_map<int32_t, uint32_t> idToRow; // for copy-table resolution
        for (const auto& sec : p.sections)
        {
            const bool offsetMap = !sec.offsetMap.empty();
            for (uint32_t i = 0; i < sec.recordCount; ++i)
            {
                const uint8_t* rec = offsetMap ? (data + sec.offsetMap[i].first)
                                               : (sec.records + static_cast<size_t>(i) * p.recordSize);

                int32_t id;
                if (offsetMap)
                    id = !sec.offsetMapIds.empty() ? static_cast<int32_t>(sec.offsetMapIds[i])
                                                   : (p.minId + static_cast<int32_t>(i));
                else
                    id = ResolveId(p, meta, sec, rec, i);

                uint32_t rowStart = static_cast<uint32_t>(out.records.size());
                out.records.resize(rowStart + out.rowSize);
                char* recOut = out.records.data() + rowStart;

                WriteVal(recOut + lay.idOffset, 4, static_cast<uint32_t>(id)); // id slot first

                for (uint32_t f = 0; f < meta.size(); ++f)
                    DecodeField(meta[f], p, rec, id, recOut);

                if (hasRel)
                {
                    auto it = sec.relationship.find(i);
                    WriteVal(recOut + lay.relOffset, 4, (it != sec.relationship.end()) ? it->second : 0u);
                }

                // Fix string fields when the raw-offset assumption does not hold (WDC2+ or multi-section).
                // stringColumns lists FIELD indices that hold a string offset.
                if (stringColumns && stringColumnCount && (p.version >= 2 || multiSection) && !offsetMap)
                {
                    for (uint32_t sc = 0; sc < stringColumnCount; ++sc)
                    {
                        uint32_t f = stringColumns[sc];
                        if (f >= meta.size() || meta[f].isInlineId) continue;
                        char* dst = recOut + meta[f].outByteOffset;
                        uint32_t raw; std::memcpy(&raw, dst, 4);
                        if (raw == 0) continue;
                        uint32_t fixed;
                        if (p.version >= 2)
                        {
                            // WDC2+ stores the offset relative to the field's own position in the file blob.
                            uint32_t fieldFilePos = sec.recordsFileOffset + i * p.recordSize + meta[f].fieldOffsetBits / 8;
                            fixed = raw + fieldFilePos - sec.stringFileOffset + sec.stringBaseInBlob;
                        }
                        else
                        {
                            fixed = raw + sec.stringBaseInBlob;
                        }
                        std::memcpy(dst, &fixed, 4);
                    }
                }

                idToRow[id] = static_cast<uint32_t>(out.ids.size());
                out.ids.push_back(id);
            }
        }

        // Copy table: each entry duplicates an existing record under a new id.
        for (const auto& sec : p.sections)
        {
            for (const auto& cp : sec.copyTable)
            {
                auto it = idToRow.find(static_cast<int32_t>(cp.second));
                if (it == idToRow.end()) continue;
                uint32_t srcStart = it->second * out.rowSize;
                uint32_t dstStart = static_cast<uint32_t>(out.records.size());
                out.records.resize(dstStart + out.rowSize);
                std::memcpy(out.records.data() + dstStart, out.records.data() + srcStart, out.rowSize);
                out.ids.push_back(static_cast<int32_t>(cp.first));
            }
        }

        return !out.ids.empty();
    }
}
