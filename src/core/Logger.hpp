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

#pragma once

/// File log, always on, Release included.
namespace wxl::core::log
{
    /**
     * @brief Opens the log file at the given path. Idempotent.
     * @param path  filesystem path of the log file.
     */
    void Open(const char* path);

    /**
     * @brief Appends one formatted line. Thread-safe.
     * @param fmt  printf-style format string followed by its arguments.
     */
    void Printf(const char* fmt, ...);

    /** 
     * @brief Flushes and closes the log file.
     */
    void Close();
}

// Record macros. All levels go to the same file; the tag is informational.
#define WLOG_INFO(...)  ::wxl::core::log::Printf(__VA_ARGS__)
#define WLOG_WARN(...)  ::wxl::core::log::Printf(__VA_ARGS__)
#define WLOG_ERROR(...) ::wxl::core::log::Printf(__VA_ARGS__)
