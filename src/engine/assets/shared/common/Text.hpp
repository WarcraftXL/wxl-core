// Case-insensitive path/string matching shared by every host and DLL asset transform. '/' and '\\'
// compare equal, so a check matches a source path regardless of which separator it used.
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
#include <string>
#include <string_view>

namespace wxl::modern::assets::common::text
{
    /**
     * @brief Reports whether `value` starts with `prefix`, case-insensitively and slash-agnostically.
     * @param value   String to test.
     * @param prefix  Prefix to match.
     * @return True if `value` starts with `prefix`.
     */
    inline bool StartsWithCI(std::string_view value, std::string_view prefix)
    {
        if (prefix.size() > value.size()) return false;
        for (size_t i = 0; i < prefix.size(); ++i)
        {
            const char a = value[i] == '/' ? '\\' : value[i];
            const char b = prefix[i] == '/' ? '\\' : prefix[i];
            if (std::tolower(static_cast<unsigned char>(a)) != std::tolower(static_cast<unsigned char>(b)))
                return false;
        }
        return true;
    }

    /**
     * @brief Reports whether `value` ends with `suffix`, case-insensitively and slash-agnostically.
     * @param value   String to test.
     * @param suffix  Suffix to match.
     * @return True if `value` ends with `suffix`.
     */
    inline bool EndsWithCI(std::string_view value, std::string_view suffix)
    {
        return suffix.size() <= value.size() &&
               StartsWithCI(value.substr(value.size() - suffix.size()), suffix);
    }

    /**
     * @brief Reports whether `value` contains `needle` anywhere, case-insensitively and slash-agnostically.
     * @param value   String to search.
     * @param needle  Substring to find.
     * @return True if `needle` occurs in `value`.
     */
    inline bool ContainsCI(std::string_view value, std::string_view needle)
    {
        if (needle.empty() || needle.size() > value.size()) return false;
        for (size_t i = 0; i + needle.size() <= value.size(); ++i)
            if (StartsWithCI(value.substr(i, needle.size()), needle)) return true;
        return false;
    }

    /**
     * @brief Lowercases a string and normalizes '/' to '\\'.
     * @param value  String to normalize.
     * @return The normalized copy.
     */
    inline std::string LowerSlashed(std::string_view value)
    {
        std::string s(value);
        for (char& c : s)
            c = c == '/' ? '\\' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }
}
