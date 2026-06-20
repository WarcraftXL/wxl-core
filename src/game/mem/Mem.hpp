// MEM game bindings: typed inline wrappers over the engine's heap allocator and free.
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

#include "game/Binding.hpp"
#include "offsets/engine/Mem.hpp"

/**
 * @brief Typed inline wrappers over the engine heap allocator and free, exposed as the MEM binding catalog.
 *
 * Blocks allocated here are freed by the engine itself, and blocks the engine allocates can be freed here.
 */
namespace wxl::game::mem
{
    namespace off = wxl::offsets::engine::mem;

    /**
     * @brief Allocates a block from the engine heap. Size is rounded up internally.
     * @param size   Requested byte count.
     * @param file   Caller file tag.
     * @param line   Caller line tag.
     * @param flags  Allocation flags.
     * @return The allocated block, or null on failure.
     */
    inline void* Alloc(uint32_t size, const char* file = "wxl", int line = 0, uint32_t flags = 0)
    {
        return Native<off::Mem_AllocFn>(off::kAlloc)(size, file, line, flags);
    }

    /**
     * @brief Frees a block obtained from the engine heap.
     * @param ptr    Block to free.
     * @param file   Caller file tag.
     * @param line   Caller line tag.
     * @param flags  Free flags.
     */
    inline void Free(void* ptr, const char* file = "wxl", int line = 0, uint32_t flags = 0)
    {
        Native<off::Mem_FreeFn>(off::kFree)(ptr, file, line, flags);
    }

    /** @brief Adds the MEM bindings to the enumerable catalog. */
    inline void RegisterCatalog()
    {
        Register({ "Mem::Alloc", off::kAlloc, "void*(size, file, line, flags)" });
        Register({ "Mem::Free",  off::kFree,  "void(ptr, file, line, flags)" });
    }
}
