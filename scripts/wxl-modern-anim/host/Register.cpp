// modern-anim host face: register the external-animation byte-transform.
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

#include "Host.hpp"

#include "../shared/ModernAnim.hpp"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace
{
    bool Transform(std::string_view name, std::span<const uint8_t> raw, std::vector<uint8_t>& out)
    {
        if (!wxl::scripts::modernanim::IsAnimName(name)) return false;
        return wxl::scripts::modernanim::ProcessCopy(raw, out);
    }

    struct Registrar
    {
        Registrar() { wxl::host::RegisterTransform("modern-anim", &Transform); }
    };

    Registrar g_registrar;
}
