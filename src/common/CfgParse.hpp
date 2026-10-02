// The WarcraftXL.cfg line format, parsed once here for the core (Config.cpp) and for extensions
// (ExtensionConfig.hpp): KEY=value lines, '#' or ';' comments, spaces trimmed.
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

#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>

namespace wxl::cfg
{
    /**
     * Interprets a raw knob value as a boolean: a leading 0, n, N, f or F is false, anything else true.
     *
     * @param string raw : the value, may be null
     * @param bool fallback : the result when raw is null or empty
     * @return bool value
     */
    inline bool Truthy(const char* raw, bool fallback)
    {
        if (!raw || !*raw) return fallback;
        const char c = *raw;
        return !(c == '0' || c == 'n' || c == 'N' || c == 'f' || c == 'F');
    }

    /**
     * Reads every KEY=value line of an open .cfg stream into the map. Comments start with '#' or ';',
     * spaces around the key and the value are dropped, a key already present keeps its first value.
     *
     * @param FILE file : an open stream, read to its end
     * @param map entries : receives the pairs
     */
    inline void Parse(FILE* file, std::unordered_map<std::string, std::string>& entries)
    {
        char line[512];
        while (std::fgets(line, sizeof line, file))
        {
            char* text = line;
            while (*text == ' ' || *text == '\t') ++text;
            if (*text == '#' || *text == ';' || *text == '\0') continue;
            char* eq = std::strchr(text, '=');
            if (!eq) continue;
            char* keyEnd = eq;
            while (keyEnd > text && (keyEnd[-1] == ' ' || keyEnd[-1] == '\t')) --keyEnd;
            char* value = eq + 1;
            while (*value == ' ' || *value == '\t') ++value;
            char* valueEnd = value + std::strlen(value);
            while (valueEnd > value && (valueEnd[-1] == '\n' || valueEnd[-1] == '\r'
                                     || valueEnd[-1] == ' '  || valueEnd[-1] == '\t')) --valueEnd;
            if (keyEnd > text)
                entries.emplace(std::string(text, keyEnd), std::string(value, valueEnd));
        }
    }

    /**
     * Parses one .cfg file; a missing file yields an empty map.
     *
     * @param string path
     * @return map entries
     */
    inline std::unordered_map<std::string, std::string> ParseFile(const char* path)
    {
        std::unordered_map<std::string, std::string> entries;
        FILE* f = nullptr;
        if (fopen_s(&f, path, "rb") != 0 || !f) return entries;
        Parse(f, entries);
        std::fclose(f);
        return entries;
    }
}
