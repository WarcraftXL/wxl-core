// Leveled logging engine shared by every WarcraftXL binary.
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

#include "common/Log.hpp"

#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace
{
    using wxl::log::Level;

    FILE*             g_file = nullptr;
    std::mutex        g_mutex;
    std::atomic<Level> g_minLevel{ Level::Info };
    bool              g_minLevelResolved = false;
    bool              g_console = false;
    char              g_consolePrefix[32] = {};

    bool EnvEnabled(const char* name, bool fallback)
    {
        const char* raw = std::getenv(name);
        if (!raw || !*raw) return fallback;
        return !(*raw == '0' || *raw == 'n' || *raw == 'N');
    }

    bool FlushEveryLine()
    {
        static const bool enabled = EnvEnabled("WXL_LOG_FLUSH", false);
        return enabled;
    }

    bool DebugStringEnabled()
    {
        static const bool enabled = EnvEnabled("WXL_LOG_DEBUGSTRING", IsDebuggerPresent() != 0);
        return enabled;
    }

    /** @brief Parses WXL_LOG_LEVEL ("debug", "warn", "2", ...) once; Info when absent/garbled. */
    Level LevelFromEnv()
    {
        const char* raw = std::getenv("WXL_LOG_LEVEL");
        if (!raw || !*raw) return Level::Info;
        if (*raw >= '0' && *raw <= '5') return static_cast<Level>(*raw - '0');
        switch (*raw | 0x20)
        {
        case 't': return Level::Trace;
        case 'd': return Level::Debug;
        case 'i': return Level::Info;
        case 'w': return Level::Warn;
        case 'e': return Level::Error;
        case 'o': return Level::Off;
        default:  return Level::Info;
        }
    }

    const char* Tag(Level level)
    {
        switch (level)
        {
        case Level::Trace: return "TRACE: ";
        case Level::Debug: return "DEBUG: ";
        case Level::Warn:  return "WARN: ";
        case Level::Error: return "ERROR: ";
        default:           return ""; // Info keeps the historical untagged format
        }
    }

    /**
     * @brief Writes one finished line to every active sink under the lock.
     * @param level  severity, drives the flush decision (O(1), no content scanning).
     * @param line   fully formatted line, newline-terminated.
     */
    void Emit(Level level, const char* line)
    {
        static unsigned pending = 0;
        std::lock_guard<std::mutex> lock(g_mutex);
        if (DebugStringEnabled()) OutputDebugStringA(line);
        if (g_console)
        {
            FILE* out = level >= Level::Warn ? stderr : stdout;
            if (g_consolePrefix[0]) fputs(g_consolePrefix, out);
            fputs(line, out);
        }
        if (g_file)
        {
            fputs(line, g_file);
            ++pending;
            if (FlushEveryLine() || pending >= 64 || level >= Level::Warn)
            {
                fflush(g_file);
                pending = 0;
            }
        }
    }
}

namespace wxl::log
{
    void Open(const char* path)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_file || !path) return;

        // Create the parent directory so a missing Logs\ folder cannot silently eat the sink
        // (the old proxy logger lost every line that way).
        char dir[MAX_PATH];
        const size_t len = strnlen(path, MAX_PATH);
        if (len < MAX_PATH)
        {
            memcpy(dir, path, len + 1);
            for (size_t i = len; i-- > 0;)
            {
                if (dir[i] == '\\' || dir[i] == '/')
                {
                    dir[i] = '\0';
                    if (dir[0]) CreateDirectoryA(dir, nullptr);
                    break;
                }
            }
        }

        fopen_s(&g_file, path, "w");
        if (g_file) setvbuf(g_file, nullptr, _IOFBF, 64 * 1024);
    }

    void EnableConsole(const char* prefix)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_console = true;
        if (prefix)
        {
            strncpy_s(g_consolePrefix, prefix, _TRUNCATE);
        }
    }

    void SetMinLevel(Level level)
    {
        g_minLevelResolved = true;
        g_minLevel.store(level, std::memory_order_relaxed);
    }

    Level MinLevel()
    {
        if (!g_minLevelResolved)
        {
            // Resolved lazily on first use; races benignly (both writers store the same value).
            g_minLevel.store(LevelFromEnv(), std::memory_order_relaxed);
            g_minLevelResolved = true;
        }
        return g_minLevel.load(std::memory_order_relaxed);
    }

    bool Enabled(Level level)
    {
        return level >= MinLevel();
    }

    void WriteV(Level level, const char* fmt, va_list args)
    {
        char body[1024];
        vsnprintf(body, sizeof(body), fmt, args);

        SYSTEMTIME t;
        GetLocalTime(&t);
        char line[1152];
        snprintf(line, sizeof(line), "[%02d:%02d:%02d] %s%s\n",
                 t.wHour, t.wMinute, t.wSecond, Tag(level), body);
        Emit(level, line);
    }

    void Write(Level level, const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        WriteV(level, fmt, args);
        va_end(args);
    }

    void Flush()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_file) fflush(g_file);
    }

    void Close()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_file) { fflush(g_file); fclose(g_file); g_file = nullptr; }
    }
}
