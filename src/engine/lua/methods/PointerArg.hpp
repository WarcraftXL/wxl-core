// Shared pointer-argument plumbing for the wxl.* readers that take a raw engine pointer.
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

#include "engine/lua/LuaJit.hpp"
#include "engine/lua/Marshal.hpp"

#include <windows.h>

#include <cstddef>
#include <cstdint>

/// The seam the pointer-taking reader contexts (wxl.doodad / wxl.wmo / wxl.m2 / wxl.charmodel) share:
/// one way to accept a raw engine pointer from Lua and one family of SEH-guarded POD reads off a
/// confirmed struct offset. The pointers arrive as LIGHTUSERDATA — exactly what the event surface hands
/// a handler (doodad_spawn, model_load, wmo_root_load, item_slot_change, ...). CheckPtr rejects any
/// other Lua type with a typed error; a NULL lightuserdata is allowed through as nullptr, and each
/// reader turns nullptr into nil rather than dereferencing it. Every SehRead* copies POD only across the
/// __try, so a stale, freed or wrong-typed pointer faults into a nil return instead of the game thread.
namespace wxl::lua::methods
{
    /// Accepts only a lightuserdata (the raw engine pointers events pass to Lua). Raises a typed Lua
    /// error for any other argument type. The returned pointer MAY be null (events legitimately push a
    /// NULL lightuserdata); callers treat null as "return nil", never dereferencing it.
    inline void* CheckPtr(lua_State* L, int idx)
    {
        if (!lua_islightuserdata(L, idx))
        {
            luaL_error(L, "expected a lightuserdata pointer (from an event) at argument %d", idx);
            return nullptr; // unreachable: luaL_error longjmps
        }
        return lua_touserdata(L, idx);
    }

    /// SEH-guarded POD read of a uint32 at base+off (false on fault). POD-only across the __try.
    inline bool SehReadU32(const void* base, size_t off, uint32_t& out)
    {
        __try
        {
            out = *reinterpret_cast<const uint32_t*>(static_cast<const char*>(base) + off);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    /// SEH-guarded POD copy of n floats starting at base+off into dst (false on fault).
    inline bool SehReadFloats(const void* base, size_t off, float* dst, int n)
    {
        __try
        {
            const float* src = reinterpret_cast<const float*>(static_cast<const char*>(base) + off);
            for (int i = 0; i < n; ++i)
                dst[i] = src[i];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    /// SEH-guarded read of a pointer slot at base+off (nullptr on fault).
    inline void* SehReadPtr(const void* base, size_t off)
    {
        __try
        {
            return *reinterpret_cast<void* const*>(static_cast<const char*>(base) + off);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    /// SEH-guarded copy of an inline NUL-terminated C string at base+off into dst, capped and
    /// NUL-terminated. Returns the copied length (0 when empty or on fault).
    inline int SehReadCStr(const void* base, size_t off, char* dst, int cap)
    {
        __try
        {
            const char* src = reinterpret_cast<const char*>(static_cast<const char*>(base) + off);
            int i = 0;
            for (; i < cap - 1 && src[i] != '\0'; ++i)
                dst[i] = src[i];
            dst[i] = '\0';
            return i;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            dst[0] = '\0';
            return 0;
        }
    }
}
