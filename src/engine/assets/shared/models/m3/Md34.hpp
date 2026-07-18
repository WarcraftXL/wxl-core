// MD34 reader for the HotS chunk-version family (BONE v1, MAT_ v19, PAR_ v24, RIB_ v9).
// Populates the generic Model / AnimLib (M3Model.hpp); a future SC2 or WoW M3 reader targeting
// different chunk versions gets its own file producing the same generic types, so M2Build and
// every other consumer never depend on which reader ran.
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

#include "M3Model.hpp"

#include <cstdint>
#include <vector>

namespace wxl::modern::assets::m3
{
    /**
     * @brief Parses a HotS-family source model into `out`, applying the model-frame transform
     *        to the geometry and rest matrices.
     * @param data   whole .m3 file bytes
     * @param size   byte count
     * @param frame  model-frame matrix (yaw fix plus lift)
     * @param out    receives the parsed model
     * @return true when the file parsed
     */
    bool ParseModel(const uint8_t* data, size_t size, const Mat4& frame, Model& out);

    /**
     * @brief Parses a HotS-family animation library (.m3a, a full MD34 with its own skeleton).
     * @param data  whole file bytes, MOVED into the library (tracks reference them)
     * @param out   receives the library
     * @return true when the file parsed
     */
    bool ParseAnimLib(std::vector<uint8_t> data, AnimLib& out);
}
