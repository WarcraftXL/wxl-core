// The registry of extension script functions, and the two seams that put them into every context.
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

#include "wxl/LuaApi.h"

namespace wxl::script
{
    /**
     * Hooks the callback check and the interface load (wxl/game/Script.hpp's two seams).
     *
     * @return bool ok : false when either seam could not be hooked; nothing is registered then
     */
    bool InstallSeams();

    /**
     * Adds a global script function to every context from now on, and to the live one.
     *
     * @param string name : copied, 63 characters at most
     * @param WXL_LuaFunction fn
     * @return bool ok : false for a null argument, a long name or a full registry
     */
    bool AddFunction(const char* name, WXL_LuaFunction fn);
}
