// Game binding pattern: typed native calls + an enumerable catalog of curated client functions.
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
#include <span>

/**
 * @brief Exposes a client function as a typed call via a plain function-pointer cast.
 *
 * The call is a zero-overhead pointer cast (no vtable, no std::function), safe in any path.
 * The catalog is a parallel enumerable registry of {name, address, signature}.
 */
namespace wxl::game
{
    /**
     * @brief One enumerable catalog entry for a curated client function.
     *
     * name is the curated, human-readable name; signature is a display/scripting-bridge string.
     */
    struct BindingInfo
    {
        const char* name;
        uintptr_t   address;
        const char* signature;
    };

    /**
     * @brief Adds an entry to the enumerable catalog.
     * @param info  the catalog entry to register.
     */
    void Register(const BindingInfo& info);

    /** @brief Returns a view over all registered catalog entries. */
    std::span<const BindingInfo> Catalog();

    /**
     * @brief Looks up a catalog entry by name.
     * @param name  the curated entry name to find.
     * @return the matching entry, or null if no entry has that name.
     */
    const BindingInfo* Find(const char* name);

    /**
     * @brief Returns a client address as a typed function pointer.
     * @param address  the client address to cast.
     * @return the address typed as Fn, for use at a call site: Native<Fn>(addr)(args...).
     */
    template <class Fn>
    inline Fn Native(uintptr_t address) { return reinterpret_cast<Fn>(address); }
}
