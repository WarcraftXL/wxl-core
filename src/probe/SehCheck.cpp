// wxl-seh-check.exe: runs the WXL_SEH_TRY guard of wxl/Common.hpp through faults, filters, nesting and
// C++ exceptions, then lets one unguarded fault reach the top-level filter. Exit code 0 means every
// check passed. Development only, never deployed; from Linux: wine build/mingw-x86/wxl-seh-check.exe.
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

#include "wxl/Common.hpp"
#include <cstdio>
#include <cstdint>
#include <stdexcept>

static int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++g_failures; } } while (0)

static DWORD Chain() { return __readfsdword(0); }

// 1. A null read inside an Any guard lands in the handler with the AV code.
static int ReadGuarded(volatile int* p, DWORD* code)
{
    WXL_SEH_TRY(wxl::seh::Catch::Any) { return *p; }
    WXL_SEH_EXCEPT { *code = WXL_SEH_CODE(); return -1; }
}

// 2. An AccessViolation guard lets another code through to the outer Any guard.
static int Outer(DWORD* code)
{
    WXL_SEH_TRY(wxl::seh::Catch::Any)
    {
        WXL_SEH_TRY(wxl::seh::Catch::AccessViolation) { RaiseException(0xE0000001, 0, 0, nullptr); return 1; }
        WXL_SEH_EXCEPT { return 2; }
    }
    WXL_SEH_EXCEPT { *code = WXL_SEH_CODE(); return 3; }
}

// 3. A nested guard catches first: the inner handler runs, the outer body carries on.
static int Nested()
{
    int r = 0;
    WXL_SEH_TRY(wxl::seh::Catch::Any)
    {
        WXL_SEH_TRY(wxl::seh::Catch::AccessViolation) { r = *static_cast<volatile int*>(nullptr); }
        WXL_SEH_EXCEPT { r = 10; }
        r += 1;
    }
    WXL_SEH_EXCEPT { r = -100; }
    return r;
}

// 4. A C++ exception thrown and caught inside the body is the inner frame's: the guard sees nothing.
static int Thrower() { throw std::runtime_error("x"); }
static int InnerCatch()
{
    try { return Thrower(); } catch (const std::exception&) { return 7; }
}
static int CppInside()
{
    WXL_SEH_TRY(wxl::seh::Catch::AccessViolation) { return InnerCatch(); }
    WXL_SEH_EXCEPT { return -1; }
}

// 5. Early return from the body unlinks the frame.
static int EarlyReturn(int v)
{
    WXL_SEH_TRY(wxl::seh::Catch::Any) { if (v) return v; return 0; }
    WXL_SEH_EXCEPT { return -1; }
}

// 6. Fault from a deeper call, through frames with their own locals.
static void Deep(int depth, volatile int* p) { if (depth) Deep(depth - 1, p); else *p = 1; }
static bool DeepGuarded()
{
    WXL_SEH_TRY(wxl::seh::Catch::AccessViolation) { Deep(50, nullptr); return false; }
    WXL_SEH_EXCEPT { return true; }
}

static LONG WINAPI TopLevel(EXCEPTION_POINTERS*)
{
    std::printf("unguarded fault reached the top: ok\n");
    std::fflush(stdout);
    ExitProcess(g_failures ? 1 : 0);
}

int main()
{
    const DWORD chain = Chain();
    DWORD code = 0;

    CHECK(ReadGuarded(nullptr, &code) == -1);
    CHECK(code == EXCEPTION_ACCESS_VIOLATION);
    CHECK(Chain() == chain);
    int v = 42;
    CHECK(ReadGuarded(&v, &code) == 42);
    CHECK(Chain() == chain);

    code = 0;
    CHECK(Outer(&code) == 3);
    CHECK(code == 0xE0000001);
    CHECK(Chain() == chain);

    CHECK(Nested() == 11);
    CHECK(Chain() == chain);

    CHECK(CppInside() == 7);
    CHECK(Chain() == chain);

    CHECK(EarlyReturn(5) == 5);
    CHECK(EarlyReturn(0) == 0);
    CHECK(Chain() == chain);

    for (int i = 0; i < 1000; ++i) CHECK(DeepGuarded());
    CHECK(Chain() == chain);

    // A fault with no guard active must still reach the top level: run it last, through the
    // unhandled filter, so a stale frame would show up as a crash or a wrong exit code.
    SetUnhandledExceptionFilter(&TopLevel);
    std::printf("%d failure(s) before the final unguarded fault\n", g_failures);
    std::fflush(stdout);
    *static_cast<volatile int*>(nullptr) = 1;
    return 2;
}
