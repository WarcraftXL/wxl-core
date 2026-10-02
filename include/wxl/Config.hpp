// Env-var + per-extension .cfg-file config reader. Header-only: extensions have no object file to
// link core's own common/Config.cpp against, so each extension DLL that includes this compiles its
// own copy instead. Same "KEY=value", '#'/';' comment, trimmed format as the core's WarcraftXL.cfg;
// env var always wins, matching the core's own precedence.
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

#include "wxl/CfgParse.hpp"

#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>

namespace wxl::ext::config
{
    /**
     * Interprets a raw knob value as a boolean: a leading 0, n, N, f or F is false, anything else true.
     *
     * @param string raw : the value, may be null
     * @param bool fallback : the result when raw is null or empty
     * @return bool value
     */
    inline bool Truthy(const char* raw, bool fallback) { return wxl::cfg::Truthy(raw, fallback); }

    /**
     * The extension's .cfg file, parsed once per DLL on the first call; later calls return that
     * first file whatever path they pass.
     *
     * @param string path
     * @return map entries
     */
    inline const std::unordered_map<std::string, std::string>& File(const char* path)
    {
        static const std::unordered_map<std::string, std::string> entries = wxl::cfg::ParseFile(path);
        return entries;
    }

    /**
     * Resolves a knob's raw value: the environment first, then the .cfg file at cfgPath.
     *
     * @param string name
     * @param string buf : receives the NUL-terminated value
     * @param uint32 cap : capacity of buf
     * @param string cfgPath
     * @return bool found
     */
    inline bool Raw(const char* name, char* buf, size_t cap, const char* cfgPath)
    {
        if (!name || !buf || !cap) return false;
        size_t written = 0;
        if (getenv_s(&written, buf, cap, name) == 0 && written > 0) return true;

        const auto& cfg = File(cfgPath);
        const auto it = cfg.find(name);
        if (it == cfg.end() || it->second.empty() || it->second.size() + 1 > cap) return false;
        std::memcpy(buf, it->second.c_str(), it->second.size() + 1);
        return true;
    }
}

namespace wxl
{
    /// The config reader, under the same wxl:: prefix as the rest of the SDK.
    namespace config = ext::config;
}
