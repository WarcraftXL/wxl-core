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

#include <cstddef>
#include <cstdint>

/**
 * @brief Exposes a client function as a typed call via a plain function-pointer cast.
 *
 * The call is a zero-overhead pointer cast (no vtable, no std::function), safe in any path.
 */
namespace wxl::game
{
    /**
     * @brief Returns a client address as a typed function pointer.
     * @param address  the client address to cast.
     * @return the address typed as Fn, for use at a call site: Native<Fn>(addr)(args...).
     */
    template <class Fn>
    inline Fn Native(uintptr_t address) { return reinterpret_cast<Fn>(address); }

    /**
     * Reads the value of type T at an absolute client address.
     *
     * @param uintptr_t address : the client address to read
     * @return T value
     */
    template <class T>
    inline T Read(uintptr_t address) { return *reinterpret_cast<const T*>(address); }

    /**
     * Stores a value at an absolute client address, as a plain store with no page-protection change.
     *
     * @param uintptr_t address : the client address to write
     * @param T value : the value to store
     */
    template <class T>
    inline void Write(uintptr_t address, const T& value) { *reinterpret_cast<T*>(address) = value; }

    /**
     * Returns a reference to the field of type T at base + offset.
     *
     * @param void* base : the start of the object
     * @param size_t offset : the byte offset of the field
     * @return T& field
     */
    template <class T>
    inline T& At(void* base, size_t offset)
    { return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + offset); }

    /**
     * Returns a reference to the field of type T at base + offset.
     *
     * @param uintptr_t base : the address of the object
     * @param size_t offset : the byte offset of the field
     * @return T& field
     */
    template <class T>
    inline T& At(uintptr_t base, size_t offset) { return *reinterpret_cast<T*>(base + offset); }

    /**
     * Returns a read-only reference to the field of type T at base + offset.
     *
     * @param const void* base : the start of the object
     * @param size_t offset : the byte offset of the field
     * @return const T& field
     */
    template <class T>
    inline const T& At(const void* base, size_t offset)
    { return *reinterpret_cast<const T*>(static_cast<const uint8_t*>(base) + offset); }

    /**
     * Returns the function at an index of an object's vtable, the vtable being the object's first pointer.
     *
     * @param const void* object : the object whose vtable to read
     * @param size_t slot : the vtable index
     * @return Fn function, or nullptr when object is null
     */
    template <class Fn>
    inline Fn Virtual(const void* object, size_t slot)
    {
        if (!object) return nullptr;
        return reinterpret_cast<Fn>((*static_cast<void* const* const*>(object))[slot]);
    }
}
