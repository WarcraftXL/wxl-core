// Game-binding catalog storage: the enumerable registry of curated client functions.
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

#include "game/Binding.hpp"

#include <cstring>
#include <vector>

namespace wxl::game
{
    namespace
    {
        /**
         * @brief Returns the function-local catalog store.
         * @return reference to the single backing vector, built on first use.
         */
        std::vector<BindingInfo>& Store()
        {
            static std::vector<BindingInfo> store;
            return store;
        }
    }

    /**
     * @brief Appends an entry to the catalog store.
     * @param info  the catalog entry to register.
     */
    void Register(const BindingInfo& info) { Store().push_back(info); }

    /** @brief Returns a view over all registered catalog entries. */
    std::span<const BindingInfo> Catalog() { return Store(); }

    /**
     * @brief Looks up a catalog entry by name.
     * @param name  the curated entry name to find.
     * @return the matching entry, or null if no entry has that name.
     */
    const BindingInfo* Find(const char* name)
    {
        for (const BindingInfo& b : Store())
            if (std::strcmp(b.name, name) == 0) return &b;
        return nullptr;
    }
}
