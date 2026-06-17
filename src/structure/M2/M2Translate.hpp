// M2 translator: de-chunk the modern container and emit a structural target model.
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
#include <vector>

#include "Client.hpp"
#include "Source.hpp"
#include "../Resolver.hpp"

// HOST-only. M2 structural translator: de-chunk the modern container and emit a target-shaped model.
// Pure bytes -> bytes. Only the structural rewrites that are independent of the live engine run here
// (de-chunk; compact cameras/particles/ribbons onto the Client strides; mask sequence blend time;
// remap sequence ids). The material/skin contract rebuild depends on the engine's parsed, pointer-fixed
// skin object and stays in the DLL runtime.
//
// Free functions, span-based, concrete. Data-gated: keyed off the container magic and the inner-version
// RANGE, never an external version flag. See structure/WMO for the worked contract pattern.
namespace wraith::structure::m2
{
    // De-chunk the container, compact the Source record layouts onto the Client strides, re-inline the
    // container TXID texture FileDataIDs as native filename strings, and emit a target-shaped model. rc
    // resolves a texture FileDataID to a path; pass an empty ResolveCtx to skip the re-inline. Returns
    // false (serve raw) when the input is already a target-shaped model or is not a model the Source path
    // handles.
    bool TranslateM2(std::span<const uint8_t> in, const ResolveCtx& rc, std::vector<uint8_t>& out);
}
