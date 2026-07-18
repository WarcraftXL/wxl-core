// Opt-in host console: runtime toggles and the print macros gated on them.
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

// Console output is opt-in at runtime via --console; the file log is always on. Two independent flags:
// the console itself, and the noisier per-open line (--console-opens). Both are set once during argument
// parsing and read from the serve and produce paths, so they live in one place instead of a shared global.
namespace wxl::host
{
    /** @brief Enables or disables console output (--console). */
    void EnableConsole(bool on);

    /** @brief Enables or disables the per-open console line (--console-opens). */
    void EnableConsoleOpenLog(bool on);

    /** @brief Reports whether console output is enabled. */
    bool ConsoleEnabled();

    /** @brief Reports whether the per-open console line is enabled. */
    bool ConsoleOpenLogEnabled();
}

#define HOST_CONSOLE(...) do { if (::wxl::host::ConsoleEnabled()) printf(__VA_ARGS__); } while (0)
#define HOST_OPEN_CONSOLE(...) \
    do { if (::wxl::host::ConsoleEnabled() && ::wxl::host::ConsoleOpenLogEnabled()) printf(__VA_ARGS__); } while (0)
