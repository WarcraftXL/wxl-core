// Declarative retail DB2 loading for runtime scripts.
// Copyright (C) 2026 WarcraftXL. GPLv3.

#pragma once

#include "Wdc5.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace wxl::runtime::db2
{
    struct TransparentStringHash
    {
        using is_transparent = void;
        size_t operator()(std::string_view value) const noexcept
        {
            return std::hash<std::string_view>{}(value);
        }
    };

    /** A physical WDC field in file order. Arrays use elements > 1. */
    struct Field
    {
        std::string_view name;
        uint16_t elements = 1;
    };

    /** Where a relationship obtains its source key. */
    enum class RelationSource : uint8_t
    {
        Field,
        RowId,
        ParentId,
    };

    /**
     * Describes a DB2 relationship without forcing either table to remain resident.  ParentId is used for
     * WDC relationship-data columns (DBD annotations `noninline,relation`).
     */
    struct Relation
    {
        std::string_view name;
        RelationSource source = RelationSource::Field;
        std::string_view sourceField;
        uint16_t sourceElement = 0;
        std::string_view targetTable;
        std::string_view targetField = "@id";
    };

    /** Immutable declaration a script supplies for one supported DB2 layout. */
    struct Definition
    {
        std::string_view name;
        std::string_view filename;
        uint32_t layoutHash = 0;
        std::span<const Field> fields;
        std::span<const Relation> relations{};
        bool required = true;
    };

    /**
     * One decoded DB2 table.  Values can be addressed by their DBD field names instead of fragile numeric
     * positions.  The object owns decoded rows but only borrows the static Definition during Load().
     */
    class Table
    {
    public:
        bool Load(const Definition& definition, std::string* error = nullptr);

        const wdc5::Row* Find(uint32_t id) const noexcept { return table_.Find(id); }
        const std::vector<wdc5::Row>& Rows() const noexcept { return table_.Rows(); }

        uint32_t Value(const wdc5::Row& row, size_t field, size_t element = 0) const noexcept;
        uint32_t Value(const wdc5::Row& row, std::string_view field, size_t element = 0) const noexcept;
        size_t FieldIndex(std::string_view field) const noexcept;
        size_t ElementCount(std::string_view field) const noexcept;
        uint32_t RelationKey(const wdc5::Row& row, std::string_view relation) const noexcept;

        std::string_view Name() const noexcept { return name_; }
        std::string_view Filename() const noexcept { return filename_; }
        uint32_t LayoutHash() const noexcept { return table_.LayoutHash(); }
        uint32_t TableHash() const noexcept { return table_.TableHash(); }
        const std::string& EmbeddedSchema() const noexcept { return table_.Schema(); }

    private:
        struct RelationBinding
        {
            RelationSource source = RelationSource::Field;
            size_t field = static_cast<size_t>(-1);
            uint16_t element = 0;
        };

        std::string name_;
        std::string filename_;
        wdc5::Table table_;
        std::unordered_map<std::string, size_t, TransparentStringHash, std::equal_to<>> fields_;
        std::unordered_map<std::string, RelationBinding, TransparentStringHash, std::equal_to<>> relations_;
    };

    /** Validates names and declared links for a related set before any large DB2 is decoded. */
    bool ValidateDefinitions(std::span<const Definition> definitions, std::string* error = nullptr);
}
