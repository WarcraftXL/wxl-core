// Shared DB2-derived item display attachments and material targeting.
// Copyright (C) 2026 WarcraftXL. GPLv3.

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace wxl::runtime::db2::itemdisplay
{
    struct GeosetFilter
    {
        uint16_t ids[16]{};
        uint32_t count = 0;
    };

    struct ModelEntry
    {
        uint32_t modelSlot = static_cast<uint32_t>(-1);
        uint32_t attachId = static_cast<uint32_t>(-1);
        uint32_t modelFlags = 0x2u | 0x4u;
        uint32_t textureFlags = 0;
        const char* folder = "";
        const char* model = "";
        const char* texture = "";
        GeosetFilter geoFilter{};
    };

    struct MaterialEntry
    {
        uint32_t modelIndex = static_cast<uint32_t>(-1);
        uint32_t modelColumn = static_cast<uint32_t>(-1);
        uint32_t layer = static_cast<uint32_t>(-1);
        uint32_t textureType = static_cast<uint32_t>(-1);
        const char* folder = "";
        const char* model = "";
        const char* texture = "";
        const char* skinSectionIds = "";
        const char* batchIndexes = "";
        const char* targetSkinSectionIds = "";
        const char* targetBatchIndexes = "";
        const char* targetMode = "";
    };

    struct Index
    {
        std::unordered_map<uint32_t, std::vector<ModelEntry>> models;
        std::unordered_map<uint32_t, std::vector<MaterialEntry>> materials;
        std::unordered_set<std::string> strings;
        // The builder publishes a model/geoset snapshot first so GlueXML and
        // ModelFrames do not wait for the much slower SKIN/material pass.
        bool materialsReady = false;

        const char* Intern(std::string_view value);
    };

    /** Atomically publishes an immutable model-only or complete snapshot. */
    void Publish(std::shared_ptr<Index> index);

    /** Returns the current snapshot, or null while the DB2 background index is still building. */
    std::shared_ptr<const Index> Current();
}
