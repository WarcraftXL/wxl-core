// Compatibility surface over the shared leveled logger (common/Log.hpp).
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

#include "common/Log.hpp"

#include <cstdarg>

/// Historical entry points, now thin forwarders into wxl::log (levels, runtime threshold,
/// compile-time floor). The WLOG_* macros live in common/Log.hpp; prefer them at new call sites.
namespace wxl::core::log
{
    /** @brief Opens the log file at the given path. Idempotent. */
    inline void Open(const char* path) { ::wxl::log::Open(path); }

    /** @brief Appends one Info line. Thread-safe; filtered by the runtime threshold. */
    inline void Printf(const char* fmt, ...)
    {
        if (!::wxl::log::Enabled(::wxl::log::Level::Info)) return;
        va_list args;
        va_start(args, fmt);
        ::wxl::log::WriteV(::wxl::log::Level::Info, fmt, args);
        va_end(args);
    }

    /** @brief Appends and immediately flushes a warning line. */
    inline void Warnf(const char* fmt, ...)
    {
        if (!::wxl::log::Enabled(::wxl::log::Level::Warn)) return;
        va_list args;
        va_start(args, fmt);
        ::wxl::log::WriteV(::wxl::log::Level::Warn, fmt, args);
        va_end(args);
    }

    /** @brief Appends and immediately flushes an error line. */
    inline void Errorf(const char* fmt, ...)
    {
        if (!::wxl::log::Enabled(::wxl::log::Level::Error)) return;
        va_list args;
        va_start(args, fmt);
        ::wxl::log::WriteV(::wxl::log::Level::Error, fmt, args);
        va_end(args);
    }

    /** @brief Flushes buffered log output without closing the file. */
    inline void Flush() { ::wxl::log::Flush(); }

    /** @brief Flushes and closes the log file. */
    inline void Close() { ::wxl::log::Close(); }
}
