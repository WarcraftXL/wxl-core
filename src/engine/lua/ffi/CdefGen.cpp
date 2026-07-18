// FFI cdef generator: derives LuaJIT struct declarations from the canonical offsets/ constants.
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

#include "engine/lua/ffi/CdefGen.hpp"

#include "offsets/game/Unit.hpp"

#include <cstdio>

namespace wxl::lua::ffi
{
    namespace
    {
        namespace off = ::wxl::offsets::game::unit;

        /// wxl_Unit mirrors off::UnitObject: an opaque pad up to the world position, then the three
        /// position floats. The pad length IS kUnitPositionField, so the Lua layout tracks the C++
        /// view (see offsets/game/Unit.hpp) automatically. LuaJIT predefines uint8_t/float.
        std::string BuildUnitCdef()
        {
            char buf[192];
            std::snprintf(buf, sizeof(buf),
                "typedef struct __attribute__((packed)) { uint8_t _pad0[%u]; float position[3]; } wxl_Unit;\n",
                static_cast<unsigned>(off::kUnitPositionField));
            return buf;
        }

        /// wxl_Model mirrors off::ModelObject: a pad up to the parent slot, then the parent pointer
        /// (void* = 4 bytes on this 32-bit build). Pad length IS kModelParentField.
        std::string BuildModelCdef()
        {
            char buf[192];
            std::snprintf(buf, sizeof(buf),
                "typedef struct __attribute__((packed)) { uint8_t _pad0[%u]; void* parent; } wxl_Model;\n",
                static_cast<unsigned>(off::kModelParentField));
            return buf;
        }
    } // namespace

    std::string BuildCoreCdefs()
    {
        return BuildUnitCdef() + BuildModelCdef();
    }
}
