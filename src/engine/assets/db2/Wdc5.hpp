// Minimal WDC5 reader used by the retail DB2 compatibility layer.
// Copyright (C) 2026 WarcraftXL. GPLv3.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace wxl::runtime::db2::wdc5
{
    struct FieldShape
    {
        uint16_t elements = 1;
    };

    struct Row
    {
        uint32_t id = 0;
        uint32_t parentId = 0;
        // Physical fields are flattened to one allocation. WDC5 item tables contain hundreds of
        // thousands of rows; a vector per field would add millions of client-heap allocations.
        std::vector<uint32_t> values;
    };

    class Table
    {
    public:
        bool Load(const void* bytes, size_t size, const std::vector<FieldShape>& shapes,
                  uint32_t expectedLayoutHash, std::string* error = nullptr);

        const Row* Find(uint32_t id) const noexcept;
        uint32_t Value(const Row& row, size_t field, size_t element = 0) const noexcept;
        size_t ElementCount(size_t field) const noexcept;
        const std::vector<Row>& Rows() const noexcept { return rows_; }
        uint32_t LayoutHash() const noexcept { return layoutHash_; }
        uint32_t TableHash() const noexcept { return tableHash_; }
        const std::string& Schema() const noexcept { return schema_; }

    private:
        uint32_t layoutHash_ = 0;
        uint32_t tableHash_ = 0;
        std::string schema_;
        std::vector<Row> rows_;
        std::unordered_map<uint32_t, size_t> byId_;
        std::vector<size_t> fieldOffsets_;
    };
}
