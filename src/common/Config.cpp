// Environment/flag-file configuration helpers shared by every WarcraftXL binary.
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

#include "common/Config.hpp"
#include "wxl/CfgParse.hpp"

#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

namespace
{
    /**
     * The user config file, parsed once per process: WarcraftXL.cfg in the working directory, else
     * one level up. The environment is consulted before it, sentinel files and defaults after.
     *
     * @return map entries
     */
    const std::unordered_map<std::string, std::string>& CfgFile()
    {
        static const std::unordered_map<std::string, std::string> entries = [] {
            auto map = wxl::cfg::ParseFile("WarcraftXL.cfg");
            if (map.empty()) map = wxl::cfg::ParseFile("..\\WarcraftXL.cfg");
            return map;
        }();
        return entries;
    }

    /**
     * @brief Resolves a knob's raw value: environment first, then the WarcraftXL.cfg file.
     * @return true when a non-empty value was found and copied into buf.
     */
    bool ReadEnv(const char* name, char* buf, DWORD cap)
    {
        if (!name) return false;
        const DWORD n = GetEnvironmentVariableA(name, buf, cap);
        if (n > 0 && n < cap) return true;

        const auto& cfg = CfgFile();
        const auto it = cfg.find(name);
        if (it == cfg.end() || it->second.empty() || it->second.size() + 1 > cap) return false;
        std::memcpy(buf, it->second.c_str(), it->second.size() + 1);
        return true;
    }
}

namespace wxl::config
{
    bool Truthy(const char* raw, bool fallback)
    {
        return wxl::cfg::Truthy(raw, fallback);
    }

    bool Raw(const char* name, char* buf, size_t cap)
    {
        return ReadEnv(name, buf, static_cast<DWORD>(cap));
    }

    bool Env(const char* name, bool fallback)
    {
        char value[16] = {};
        if (!ReadEnv(name, value, sizeof value)) return fallback;
        return Truthy(value, fallback);
    }

    bool Flag(const char* envName, const char* disableFile)
    {
        char value[16] = {};
        if (ReadEnv(envName, value, sizeof value) && !Truthy(value, true))
            return false;
        if (disableFile && GetFileAttributesA(disableFile) != INVALID_FILE_ATTRIBUTES)
            return false;
        return true;
    }

    uint64_t U64(const char* name, uint64_t fallback, uint64_t minValue, uint64_t maxValue)
    {
        char value[32] = {};
        if (!ReadEnv(name, value, sizeof value)) return fallback;
        char* end = nullptr;
        const uint64_t parsed = std::strtoull(value, &end, 10);
        if (end == value) return fallback;
        if (parsed < minValue) return minValue;
        if (parsed > maxValue) return maxValue;
        return parsed;
    }

    uint32_t U32(const char* name, uint32_t fallback, uint32_t minValue, uint32_t maxValue)
    {
        return static_cast<uint32_t>(U64(name, fallback, minValue, maxValue));
    }

    uint32_t BytesMbKb(const char* envMb, const char* envKb, uint32_t defBytes,
                       uint32_t minKb, uint32_t maxKb)
    {
        char value[32] = {};
        if (ReadEnv(envMb, value, sizeof value))
        {
            char* end = nullptr;
            const uint64_t mb = std::strtoull(value, &end, 10);
            const uint64_t kb = mb * 1024ull;
            if (end != value && kb >= minKb && kb <= maxKb)
                return static_cast<uint32_t>(kb * 1024ull);
        }
        if (ReadEnv(envKb, value, sizeof value))
        {
            char* end = nullptr;
            const uint64_t kb = std::strtoull(value, &end, 10);
            if (end != value && kb >= minKb && kb <= maxKb)
                return static_cast<uint32_t>(kb * 1024ull);
        }
        return defBytes;
    }
}
