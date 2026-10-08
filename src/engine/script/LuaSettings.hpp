// Settings sets reachable from Lua by name: the WXL_Settings_* script functions (wxl/LuaApi.h).
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
    /// Registers the WXL_Settings_* functions. After InstallSeams.
    bool InstallSettingsFunctions();

    /**
     * Adds a settings set (or replaces the one of the same name).
     *
     * @param WXL_SettingsSet* set : must outlive the process
     * @return bool ok : false for a bad set or a full table (32)
     */
    bool AddSettings(const WXL_SettingsSet* set);

    /// The sets registered, in registration order.
    uint32_t SettingsCount();
    const WXL_SettingsSet* SettingsAt(uint32_t index);
}
