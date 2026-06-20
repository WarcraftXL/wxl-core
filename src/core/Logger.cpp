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
#include <mutex>

namespace
{
    FILE*      g_file = nullptr;
    std::mutex g_mutex;

    /**
     * @brief Appends one finished line under the lock and mirrors it to the debugger.
     * @param line  fully formatted line to write.
     */
    void Emit(const char* line)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        OutputDebugStringA(line);
        if (g_file) { fputs(line, g_file); fflush(g_file); }
    }
}

namespace wxl::core::log
{
    /**
     * @brief Opens the log file at the given path. Idempotent.
     * @param path  filesystem path of the log file.
     */
    void Open(const char* path)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_file) return;
        fopen_s(&g_file, path, "w");
    }

    /**
     * @brief Appends one formatted line prefixed with the local time. Thread-safe.
     * @param fmt  printf-style format string followed by its arguments.
     */
    void Printf(const char* fmt, ...)
    {
        char body[1024];
        va_list args;
        va_start(args, fmt);
        vsnprintf(body, sizeof(body), fmt, args);
        va_end(args);

        SYSTEMTIME t;
        GetLocalTime(&t);
        char line[1152];
        snprintf(line, sizeof(line), "[%02d:%02d:%02d] %s\n",
                 t.wHour, t.wMinute, t.wSecond, body);
        Emit(line);
    }

    /** 
     * @brief Flushes and closes the log file.
     */
    void Close()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_file) { fclose(g_file); g_file = nullptr; }
    }
}
