// Page-protection helpers shared by every binary that patches code or vtables in place.
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

#include <windows.h>
#include <cstddef>

/**
 * @brief The one audited protect-write-restore implementation.
 *
 * Header-only so the d3d9 proxy (which does not link core) shares it. Replaces the three
 * hand-rolled VirtualProtect dances that used to live in core/Mem, RenderHooks, and Capture —
 * one place to guarantee the restore actually happens and the instruction cache is flushed.
 */
namespace wxl::mem
{
    /**
     * @brief Makes [dst, dst+len) writable, runs `write`, restores the previous protection.
     * @param dst    start of the range to unprotect.
     * @param len    byte count.
     * @param write  callable performing the writes while the range is writable.
     * @return false when the initial unprotect failed (write never ran).
     */
    template <class Fn>
    bool WithWritable(void* dst, size_t len, Fn&& write)
    {
        DWORD old = 0;
        if (!VirtualProtect(dst, len, PAGE_EXECUTE_READWRITE, &old)) return false;
        write();
        DWORD restored = 0;
        VirtualProtect(dst, len, old, &restored);
        FlushInstructionCache(GetCurrentProcess(), dst, len);
        return true;
    }

    /**
     * @brief Swaps one (typically vtable) pointer slot, returning the previous value.
     * @param slot      slot to rewrite.
     * @param value     new pointer to store.
     * @param previous  receives the replaced pointer, may be null.
     * @return false when the slot could not be made writable.
     */
    inline bool SwapPointer(void** slot, void* value, void** previous)
    {
        return WithWritable(slot, sizeof(void*), [&] {
            if (previous) *previous = *slot;
            *slot = value;
        });
    }
}
