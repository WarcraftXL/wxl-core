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

#include <windows.h>
#include <cstdlib>

namespace
{
    /**
     * @brief Reads an environment variable into a small buffer.
     * @return true when present and non-empty.
     */
    bool ReadEnv(const char* name, char* buf, DWORD cap)
    {
        if (!name) return false;
        const DWORD n = GetEnvironmentVariableA(name, buf, cap);
        return n > 0 && n < cap;
    }
}

namespace wxl::config
{
    bool Truthy(const char* raw, bool fallback)
    {
        if (!raw || !*raw) return fallback;
        const char c = *raw;
        return !(c == '0' || c == 'n' || c == 'N' || c == 'f' || c == 'F');
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
