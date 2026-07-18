// Single include point for the vendored LuaJIT C headers, wrapped for C++ linkage.
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

// LuaJIT is a C library; its headers must be pulled in under C linkage so the mangled C++
// names do not diverge from the archive's exported symbols. Include path resolves to
// deps/luajit/src (see docs/vm-build-notes.md). Only the three always-present headers are
// used at the MVP: luajit.h is generated at build time and is added here the day the
// luaJIT_* surface (setmode, profiler) is needed.
extern "C"
{
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
