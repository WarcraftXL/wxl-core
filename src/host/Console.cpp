// Opt-in host console: runtime toggles behind the print macros.
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

#include "Console.hpp"

namespace wxl::host
{
    namespace
    {
        // Set once during argument parsing, before any worker starts; read-only thereafter.
        bool g_console = false;
        bool g_consoleOpenLog = false;
    }

    void EnableConsole(bool on) { g_console = on; }
    void EnableConsoleOpenLog(bool on) { g_consoleOpenLog = on; }
    bool ConsoleEnabled() { return g_console; }
    bool ConsoleOpenLogEnabled() { return g_consoleOpenLog; }
}
