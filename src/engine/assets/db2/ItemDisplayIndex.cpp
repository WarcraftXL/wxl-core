// Shared DB2-derived item display attachments and material targeting.
// Copyright (C) 2026 WarcraftXL. GPLv3.

#include "ItemDisplayIndex.hpp"

#include <atomic>

namespace wxl::runtime::db2::itemdisplay
{
    namespace
    {
        std::shared_ptr<const Index> g_current;
    }

    const char* Index::Intern(std::string_view value)
    {
        if (value.empty()) return "";
        return strings.emplace(value).first->c_str();
    }

    void Publish(std::shared_ptr<Index> index)
    {
        std::atomic_store_explicit(&g_current, std::shared_ptr<const Index>(std::move(index)),
                                   std::memory_order_release);
    }

    std::shared_ptr<const Index> Current()
    {
        return std::atomic_load_explicit(&g_current, std::memory_order_acquire);
    }
}
