// ADT translator: merge a split modern ADT set into a monolithic 335 ADT.
// Copyright (C) 2026 WraithEngine
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
#include <string_view>
#include <vector>

#include "../Resolver.hpp"

// Client.hpp / Source.hpp declare this format's two layouts (335 target / modern superset). The
// transform keeps its own local constants for now; consuming the declarations is a later no-op pass.

// HOST-only. ADT translator: merge a split modern ADT set into a monolithic 335 ADT. Pure bytes -> 335 bytes. Free functions, span-based, concrete.
// The transform gates on chunk/field presence. See structure/WMO for the worked example of the contract pattern.
namespace wraith::structure::adt
{
    // Merge split root + _tex0 + _obj0 into one monolithic 335 ADT. `name` is used only for the probe log
    // that surfaces tiles exceeding the native 4-layer cap. `rc` resolves texture FileDataIDs (MDID) to
    // paths when the source references textures by id instead of an MTEX name table (the modern default).
    bool MergeSplitAdt(std::span<const uint8_t> root, std::span<const uint8_t> tex0,
                       std::span<const uint8_t> obj0, std::vector<uint8_t>& out, std::string_view name,
                       const ResolveCtx& rc);
}
