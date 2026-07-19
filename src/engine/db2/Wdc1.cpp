// Generic WDC1 reader over an in-memory byte buffer -- implementation.
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

#include "engine/db2/Wdc1.hpp"

#include "common/Log.hpp"

#include <cstring>

namespace wxl::db2
{
    namespace
    {
        constexpr uint32_t kMagic = 0x31434457; // 'WDC1' little-endian ('W''D''C''1')
        constexpr size_t   kHeaderSize = 0x54;

        // WDC1 header field byte offsets (only the ones the fixed-record subset needs).
        constexpr size_t kOffRecordCount     = 0x04;
        constexpr size_t kOffFieldCount      = 0x08;
        constexpr size_t kOffRecordSize      = 0x0C;
        constexpr size_t kOffStringTableSize = 0x10;
        constexpr size_t kOffCopyTableSize   = 0x28;
        constexpr size_t kOffFlags           = 0x2C; // u16
        constexpr size_t kOffIdIndex         = 0x2E; // u16
        constexpr size_t kOffIdListSize      = 0x40;
        constexpr size_t kOffFieldStorageSz  = 0x44;
        constexpr size_t kOffCommonDataSize  = 0x48;
        constexpr size_t kOffPalletDataSize  = 0x4C;
        constexpr size_t kOffRelationDataSz  = 0x50;
    }

    uint32_t Wdc1::RdU32(size_t off) const
    {
        uint32_t v = 0;
        std::memcpy(&v, data_.data() + off, 4);
        return v;
    }

    bool Wdc1::Load(std::vector<uint8_t> bytes)
    {
        *this = Wdc1{};
        data_ = std::move(bytes);
        if (data_.size() < kHeaderSize)
        {
            WLOG_ERROR("[db2] file too small (%zu bytes)", data_.size());
            return false;
        }
        if (RdU32(0) != kMagic)
        {
            WLOG_ERROR("[db2] bad magic 0x%08X (expected WDC1)", RdU32(0));
            return false;
        }

        recordCount_     = RdU32(kOffRecordCount);
        fieldCount_      = RdU32(kOffFieldCount);
        recordSize_      = RdU32(kOffRecordSize);
        stringTableSize_ = RdU32(kOffStringTableSize);
        const uint16_t flags = static_cast<uint16_t>(RdU32(kOffFlags) & 0xFFFF);
        idIndex_         = static_cast<uint16_t>(RdU32(kOffIdIndex) & 0xFFFF);

        // Refuse anything outside the plain fixed-record subset -- see the header note. A future DB2
        // that trips this needs the richer WDC1 features implemented, not a silent misread.
        const uint32_t idList   = RdU32(kOffIdListSize);
        const uint32_t pallet   = RdU32(kOffPalletDataSize);
        const uint32_t common   = RdU32(kOffCommonDataSize);
        const uint32_t relation = RdU32(kOffRelationDataSz);
        const uint32_t copyTbl  = RdU32(kOffCopyTableSize);
        if (flags != 0 || idList != 0 || pallet != 0 || common != 0 || relation != 0 || copyTbl != 0)
        {
            WLOG_ERROR("[db2] out-of-subset WDC1 (flags=%u idList=%u pallet=%u common=%u rel=%u copy=%u)",
                       flags, idList, pallet, common, relation, copyTbl);
            return false;
        }
        if (fieldCount_ == 0 || recordSize_ == 0 || fieldCount_ > 256)
        {
            WLOG_ERROR("[db2] implausible field/record layout (fields=%u recSize=%u)", fieldCount_, recordSize_);
            return false;
        }

        // field_structure: fieldCount x {int16 sizeMarker, uint16 position}, right after the header.
        const size_t fieldStructAt = kHeaderSize;
        recStart_ = fieldStructAt + static_cast<size_t>(fieldCount_) * 4;
        strStart_ = recStart_ + static_cast<size_t>(recordCount_) * recordSize_;
        if (strStart_ + stringTableSize_ > data_.size())
        {
            WLOG_ERROR("[db2] truncated: strEnd=%zu > size=%zu", strStart_ + stringTableSize_, data_.size());
            return false;
        }

        fieldPos_.resize(fieldCount_);
        for (uint32_t i = 0; i < fieldCount_; ++i)
            fieldPos_[i] = static_cast<uint16_t>(RdU32(fieldStructAt + i * 4) >> 16); // position = high u16

        if (idIndex_ >= fieldCount_)
        {
            WLOG_ERROR("[db2] id_index %u out of range (fields=%u)", idIndex_, fieldCount_);
            return false;
        }

        ok_ = true;
        WLOG_DEBUG("[db2] loaded WDC1: %u records, %u fields, recSize=%u, strtab=%u",
                   recordCount_, fieldCount_, recordSize_, stringTableSize_);
        return true;
    }

    uint32_t Wdc1::U32(uint32_t rec, uint32_t field) const
    {
        if (!ok_ || rec >= recordCount_ || field >= fieldCount_)
            return 0;
        const size_t pos = fieldPos_[field];
        if (pos + 4 > recordSize_)
            return 0;
        const size_t off = recStart_ + static_cast<size_t>(rec) * recordSize_ + pos;
        return RdU32(off);
    }

    uint32_t Wdc1::Id(uint32_t rec) const
    {
        return U32(rec, idIndex_);
    }

    const char* Wdc1::Str(uint32_t rec, uint32_t field) const
    {
        if (!ok_)
            return "";
        const uint32_t offset = U32(rec, field);
        const size_t at = strStart_ + offset;
        if (offset >= stringTableSize_ || at >= data_.size())
            return "";
        return reinterpret_cast<const char*>(data_.data() + at);
    }

    int Wdc1::IndexOfId(uint32_t id) const
    {
        if (!ok_ || recordCount_ == 0)
            return -1;
        // Records are id-sorted ascending in these tables; binary search. (A stray unsorted table
        // would only cause a miss, never a bad read -- callers treat -1 as "unresolved".)
        uint32_t lo = 0, hi = recordCount_;
        while (lo < hi)
        {
            const uint32_t mid = lo + (hi - lo) / 2;
            const uint32_t v = Id(mid);
            if (v == id) return static_cast<int>(mid);
            if (v < id)  lo = mid + 1;
            else         hi = mid;
        }
        return -1;
    }
}
