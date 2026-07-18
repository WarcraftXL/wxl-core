// Internal path helpers shared by the MpqStore mount/index and read paths.
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

#include <windows.h>
#include <cctype>
#include <string>
#include <string_view>

// Small name/disk helpers used by both MpqStore translation units (mount/index and read). Kept here so the
// two files share one definition instead of each carrying a copy.
namespace wxl::host::mpq::detail
{
    // Only the item subtree is indexed for by-name resolution.
    constexpr const char* kIndexPrefix = "item\\";

    /**
     * @brief Reports whether `path` names an existing file on disk (not a directory).
     * @param path  absolute file path
     * @return true if the file exists
     */
    inline bool FileExistsOnDisk(const std::string& path)
    {
        DWORD a = GetFileAttributesA(path.c_str());
        return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
    }

    /**
     * @brief Returns a lowercased copy of `s`.
     * @param s  input string
     * @return lowercased string
     */
    inline std::string ToLower(std::string s)
    {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    /**
     * @brief Reshapes a client file name to the archive-internal form: backslashes, no leading separators.
     * @param name  file name from the client (either separator)
     * @return archive-internal name
     */
    inline std::string NormalizeName(std::string_view name)
    {
        std::string n(name);
        for (char& c : n) if (c == '/') c = '\\';
        size_t i = 0;
        while (i < n.size() && n[i] == '\\') ++i;
        return n.substr(i);
    }
}
