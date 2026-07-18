// Shared asset-transform environment toggles, read once and cached for the process lifetime.
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

#include <cctype>
#include <cstdlib>

namespace wxl::modern::assets::common::env
{
    /**
     * @brief Parses a boolean-ish environment value ("0"/"n"/"f" and their variants are false).
     * @param raw  Raw environment value, possibly null.
     * @return True if `raw` is set and does not read as false.
     */
    inline bool Truthy(const char* raw)
    {
        if (!raw || !*raw) return false;
        const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(*raw)));
        return c != '0' && c != 'n' && c != 'f';
    }

    /**
     * @brief Reports whether per-asset transform logging (WXL_VERBOSE_ASSET_LOGS or WXL_ASSET_LOGS) is on.
     * @return True if verbose asset logging is enabled.
     */
    inline bool VerboseAssetLogs()
    {
        static const bool enabled = [] {
            const char* raw = std::getenv("WXL_VERBOSE_ASSET_LOGS");
            if (!raw || !*raw) raw = std::getenv("WXL_ASSET_LOGS");
            return Truthy(raw);
        }();
        return enabled;
    }
}
