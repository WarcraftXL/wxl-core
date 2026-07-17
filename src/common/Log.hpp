// Leveled logging engine shared by every WarcraftXL binary (DLL, proxy, patcher, host).
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

#include <cstdarg>
#include <cstdint>

/**
 * @brief Severity-leveled logging with a compile-time floor and a runtime threshold.
 *
 * Each binary owns its own instance of this engine (static per module), so the DLL, the d3d9
 * proxy, the patcher, and the host naturally write to separate sinks. A call below the runtime
 * threshold costs one relaxed atomic load — no formatting, no lock, no timestamp.
 *
 * Runtime threshold: WXL_LOG_LEVEL env var (trace|debug|info|warn|error|off, or 0-5); defaults
 * to Info. Compile-time floor: WXL_LOG_COMPILE_MIN (numeric Level value); Release strips Trace,
 * Debug builds keep everything. Flushing is by severity (Warn and above), plus every 64 buffered
 * lines, plus WXL_LOG_FLUSH=1 to flush every line.
 */
namespace wxl::log
{
    enum class Level : uint8_t
    {
        Trace = 0, // per-item spam useful only when chasing one specific bug
        Debug = 1, // per-event diagnostics (per-model, per-open); off by default
        Info  = 2, // startup summaries, one-shot confirmations, periodic profiles
        Warn  = 3, // unexpected but survivable; flushed immediately
        Error = 4, // broken functionality; flushed immediately
        Off   = 5, // threshold-only value: disables all output
    };

    /**
     * @brief Opens the file sink. Idempotent; creates the parent directory if missing.
     * @param path  filesystem path of the log file (truncated: one file per session).
     */
    void Open(const char* path);

    /**
     * @brief Enables the console sink (for CLI tools): Warn+ to stderr, below to stdout.
     * @param prefix  optional tag prepended to every console line (e.g. "[WarcraftXL] "), may be null.
     */
    void EnableConsole(const char* prefix);

    /** @brief Sets the runtime threshold. Overrides the WXL_LOG_LEVEL default. */
    void SetMinLevel(Level level);

    /** @brief Returns the current runtime threshold. */
    Level MinLevel();

    /**
     * @brief Cheap gate: true when `level` would be written.
     *
     * Call sites use this (via the WLOG_* macros) to skip argument formatting entirely for
     * filtered lines.
     */
    bool Enabled(Level level);

    /** @brief Formats and writes one line at `level`. Thread-safe. */
    void Write(Level level, const char* fmt, ...);

    /** @brief va_list variant of Write. */
    void WriteV(Level level, const char* fmt, va_list args);

    /** @brief Flushes buffered output without closing the file. */
    void Flush();

    /** @brief Flushes and closes the file sink. */
    void Close();
}

// Compile-time floor: levels below it compile to nothing (arguments never evaluated).
// Release strips Trace; Debug builds keep everything. Override with -DWXL_LOG_COMPILE_MIN=N.
#ifndef WXL_LOG_COMPILE_MIN
#  ifdef NDEBUG
#    define WXL_LOG_COMPILE_MIN 1
#  else
#    define WXL_LOG_COMPILE_MIN 0
#  endif
#endif

#define WXL_LOG_AT(lvl, ...) \
    do { if (::wxl::log::Enabled(lvl)) ::wxl::log::Write(lvl, __VA_ARGS__); } while (0)

#if WXL_LOG_COMPILE_MIN <= 0
#  define WLOG_TRACE(...) WXL_LOG_AT(::wxl::log::Level::Trace, __VA_ARGS__)
#else
#  define WLOG_TRACE(...) ((void)0)
#endif

#if WXL_LOG_COMPILE_MIN <= 1
#  define WLOG_DEBUG(...) WXL_LOG_AT(::wxl::log::Level::Debug, __VA_ARGS__)
#else
#  define WLOG_DEBUG(...) ((void)0)
#endif

#define WLOG_INFO(...)  WXL_LOG_AT(::wxl::log::Level::Info,  __VA_ARGS__)
#define WLOG_WARN(...)  WXL_LOG_AT(::wxl::log::Level::Warn,  __VA_ARGS__)
#define WLOG_ERROR(...) WXL_LOG_AT(::wxl::log::Level::Error, __VA_ARGS__)
