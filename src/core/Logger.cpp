// File log, always on.
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

#include "core/Logger.hpp"

#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace
{
    FILE*      g_file = nullptr;
    std::mutex g_mutex;

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

    bool IsImportant(const char* line)
    {
        return std::strstr(line, "ERROR") ||
               std::strstr(line, "WARN") ||
               std::strstr(line, "Fatal") ||
               std::strstr(line, "fatal") ||
               std::strstr(line, "crashed") ||
               std::strstr(line, "failed");
    }

    /**
     * @brief Appends one finished line under the lock and mirrors it to the debugger.
     * @param line  fully formatted line to write.
     */
    void Emit(const char* line)
    {
        static unsigned pending = 0;
        std::lock_guard<std::mutex> lock(g_mutex);
        if (DebugStringEnabled()) OutputDebugStringA(line);
        if (g_file)
        {
            fputs(line, g_file);
            ++pending;
            if (FlushEveryLine() || pending >= 64 || IsImportant(line))
            {
                fflush(g_file);
                pending = 0;
            }
        }
    }
}

namespace wxl::core::log
{
    /** @brief Formats one optional-level line and hands it to the synchronized sink. */
    void VPrintf(const char* level, const char* fmt, va_list args)
    {
        char body[1024];
        vsnprintf(body, sizeof(body), fmt, args);

        SYSTEMTIME t;
        GetLocalTime(&t);
        char line[1152];
        if (level)
            snprintf(line, sizeof(line), "[%02d:%02d:%02d] %s: %s\n",
                     t.wHour, t.wMinute, t.wSecond, level, body);
        else
            snprintf(line, sizeof(line), "[%02d:%02d:%02d] %s\n",
                     t.wHour, t.wMinute, t.wSecond, body);
        Emit(line);
    }

    /**
     * @brief Opens the log file at the given path. Idempotent.
     * @param path  filesystem path of the log file.
     */
    void Open(const char* path)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_file) return;
        fopen_s(&g_file, path, "w");
        if (g_file) setvbuf(g_file, nullptr, _IOFBF, 64 * 1024);
    }

    /**
     * @brief Appends one formatted line prefixed with the local time. Thread-safe.
     * @param fmt  printf-style format string followed by its arguments.
     */
    void Printf(const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        VPrintf(nullptr, fmt, args);
        va_end(args);
    }

    /** @brief Appends a warning line; the WARN tag makes the buffered sink flush it immediately. */
    void Warnf(const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        VPrintf("WARN", fmt, args);
        va_end(args);
    }

    /** @brief Appends an error line; the ERROR tag makes the buffered sink flush it immediately. */
    void Errorf(const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        VPrintf("ERROR", fmt, args);
        va_end(args);
    }

    /** @brief Flushes buffered log output without closing the file. */
    void Flush()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_file) fflush(g_file);
    }

    /** 
     * @brief Flushes and closes the log file.
     */
    void Close()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_file) { fflush(g_file); fclose(g_file); g_file = nullptr; }
    }
}
