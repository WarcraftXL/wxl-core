// Declarative retail DB2 loading for runtime scripts.
// Copyright (C) 2026 WarcraftXL. GPLv3.

#include "Db2.hpp"

#include "game/io/Io.hpp"
#include "offsets/engine/Io.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <limits>
#include <unordered_set>

namespace wxl::runtime::db2
{
    namespace
    {
        constexpr size_t kMissingField = static_cast<size_t>(-1);

        bool Fail(std::string* error, std::string message)
        {
            if (error) *error = std::move(message);
            return false;
        }

        std::string ArchivePath(std::string_view filename)
        {
            std::string path(filename);
            std::replace(path.begin(), path.end(), '/', '\\');
            if (path.find('\\') == std::string::npos) path.insert(0, "DBFilesClient\\");
            return path;
        }

        bool ReadWholeFile(std::string_view filename, std::vector<uint8_t>& bytes)
        {
            bytes.clear();
            const std::string path = ArchivePath(filename);
            auto readLoose = [&bytes](const std::string& loosePath) {
                std::ifstream stream(loosePath, std::ios::binary);
                if (!stream) return false;
                bytes.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
                return !bytes.empty();
            };
            if (readLoose(path)) return true;
            if (readLoose("Data\\Patch-Z.MPQ\\" + path)) return true;

            namespace io = wxl::game::io;
            namespace iooff = wxl::offsets::engine::io;
            void* handle = nullptr;
            if (!io::FileOpen(path.c_str(), iooff::kOpenWholeFile, &handle) || !handle) return false;
            uint32_t high = 0;
            const uint32_t size = io::FileSize(handle, &high);
            if (!size || high)
            {
                io::FileClose(handle);
                return false;
            }
            bytes.resize(size);
            uint32_t got = 0;
            const bool ok = io::FileRead(handle, bytes.data(), size, &got) != 0 && got == size;
            io::FileClose(handle);
            if (!ok) bytes.clear();
            return ok;
        }

        const Definition* FindDefinition(std::span<const Definition> definitions, std::string_view name)
        {
            for (const Definition& definition : definitions)
                if (definition.name == name) return &definition;
            return nullptr;
        }

        bool HasField(const Definition& definition, std::string_view name)
        {
            if (name == "@id" || name == "@parent") return true;
            return std::ranges::any_of(definition.fields,
                [name](const Field& field) { return field.name == name; });
        }
    }

    bool Table::Load(const Definition& definition, std::string* error)
    {
        name_.clear();
        filename_.clear();
        fields_.clear();
        relations_.clear();
        table_ = {};
        if (error) error->clear();
        if (definition.name.empty() || definition.filename.empty())
            return Fail(error, "DB2: table name and filename are required");
        if (definition.fields.empty())
            return Fail(error, "DB2: " + std::string(definition.name) + " has no physical fields");

        std::vector<wdc5::FieldShape> shapes;
        shapes.reserve(definition.fields.size());
        fields_.reserve(definition.fields.size());
        for (size_t i = 0; i < definition.fields.size(); ++i)
        {
            const Field& field = definition.fields[i];
            if (field.name.empty() || !fields_.emplace(std::string(field.name), i).second)
                return Fail(error, "DB2: " + std::string(definition.name) + " has an empty or duplicate field name");
            shapes.push_back({std::max<uint16_t>(1, field.elements)});
        }

        for (const Relation& relation : definition.relations)
        {
            RelationBinding binding{relation.source, kMissingField, relation.sourceElement};
            if (relation.source == RelationSource::Field)
            {
                const auto field = fields_.find(relation.sourceField);
                if (field == fields_.end())
                    return Fail(error, "DB2: relation " + std::string(relation.name) + " names an unknown source field");
                binding.field = field->second;
                if (binding.element >= std::max<uint16_t>(1, definition.fields[binding.field].elements))
                    return Fail(error, "DB2: relation " + std::string(relation.name) + " has an invalid array element");
            }
            if (relation.name.empty() || !relations_.emplace(std::string(relation.name), binding).second)
                return Fail(error, "DB2: " + std::string(definition.name) + " has an empty or duplicate relation name");
        }

        std::vector<uint8_t> bytes;
        if (!ReadWholeFile(definition.filename, bytes))
            return Fail(error, "DB2: unable to read " + ArchivePath(definition.filename));
        if (!table_.Load(bytes.data(), bytes.size(), shapes, definition.layoutHash, error)) return false;

        name_.assign(definition.name);
        filename_.assign(definition.filename);
        return true;
    }

    uint32_t Table::Value(const wdc5::Row& row, size_t field, size_t element) const noexcept
    {
        return table_.Value(row, field, element);
    }

    uint32_t Table::Value(const wdc5::Row& row, std::string_view field, size_t element) const noexcept
    {
        const size_t index = FieldIndex(field);
        return index == kMissingField ? 0 : table_.Value(row, index, element);
    }

    size_t Table::FieldIndex(std::string_view field) const noexcept
    {
        const auto it = fields_.find(field);
        return it == fields_.end() ? kMissingField : it->second;
    }

    size_t Table::ElementCount(std::string_view field) const noexcept
    {
        const size_t index = FieldIndex(field);
        return index == kMissingField ? 0 : table_.ElementCount(index);
    }

    uint32_t Table::RelationKey(const wdc5::Row& row, std::string_view relation) const noexcept
    {
        const auto it = relations_.find(relation);
        if (it == relations_.end()) return 0;
        switch (it->second.source)
        {
            case RelationSource::RowId: return row.id;
            case RelationSource::ParentId: return row.parentId;
            case RelationSource::Field: return table_.Value(row, it->second.field, it->second.element);
        }
        return 0;
    }

    bool ValidateDefinitions(std::span<const Definition> definitions, std::string* error)
    {
        if (error) error->clear();
        std::unordered_set<std::string> names;
        for (const Definition& definition : definitions)
        {
            if (definition.name.empty() || definition.filename.empty() || definition.fields.empty())
                return Fail(error, "DB2: every definition needs a name, filename, and physical fields");
            if (!names.emplace(definition.name).second)
                return Fail(error, "DB2: duplicate table definition " + std::string(definition.name));
            std::unordered_set<std::string> fields;
            for (const Field& field : definition.fields)
                if (field.name.empty() || !fields.emplace(field.name).second)
                    return Fail(error, "DB2: duplicate or empty field in " + std::string(definition.name));
        }

        for (const Definition& definition : definitions)
        {
            for (const Relation& relation : definition.relations)
            {
                if (relation.name.empty() || relation.targetTable.empty())
                    return Fail(error, "DB2: incomplete relation in " + std::string(definition.name));
                if (relation.source == RelationSource::Field && !HasField(definition, relation.sourceField))
                    return Fail(error, "DB2: relation source field is absent from " + std::string(definition.name));
                const Definition* target = FindDefinition(definitions, relation.targetTable);
                if (!target)
                    return Fail(error, "DB2: relation target table " + std::string(relation.targetTable) + " is not declared");
                if (!HasField(*target, relation.targetField))
                    return Fail(error, "DB2: relation target field is absent from " + std::string(target->name));
            }
        }
        return true;
    }
}
