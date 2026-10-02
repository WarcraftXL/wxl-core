// The C++ half of the toolchain-neutral layer: the structured-exception guard and small helpers.
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

#include "wxl/Common.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

// =====================================================================================================
// Structured exceptions
//
//   WXL_SEH_TRY(wxl::seh::Catch::Any)             { ... } WXL_SEH_EXCEPT { ... }
//   WXL_SEH_TRY(wxl::seh::Catch::AccessViolation) { ... } WXL_SEH_EXCEPT { ... WXL_SEH_CODE() ... }
//
// A guard around raw client memory: a fault in the body lands in the handler instead of killing the
// process. MSVC, clang-cl and 64-bit clang compile __try natively. 32-bit clang and GCC for mingw
// cannot emit the x86 SEH tables, so there the same guard is made by hand: a frame on the fs:[0]
// handler chain, left through __builtin_longjmp. The rule of __try still holds on every compiler:
// no object with a destructor lives in the guarded function (MSVC C2712). Leaving the body early
// (return, break, goto) is fine.
// =====================================================================================================
namespace wxl::seh
{
    /// What a guard catches: every exception, or an access violation only (anything else keeps searching).
    enum class Catch : int { Any = 0, AccessViolation = 1 };

    inline bool Accepts(Catch what, DWORD code) noexcept
    {
        return what == Catch::Any || code == EXCEPTION_ACCESS_VIOLATION;
    }
}

#if WXL_MSVC_FRONTEND || (WXL_COMPILER_CLANG && WXL_ARCH_X64)

// Native SEH. The if-with-initialiser carries the mode from the try to the filter.
#define WXL_SEH_TRY(what) if (const ::wxl::seh::Catch wxl_seh_what = (what); true) __try
#define WXL_SEH_EXCEPT \
    __except (::wxl::seh::Accepts(wxl_seh_what, GetExceptionCode()) ? EXCEPTION_EXECUTE_HANDLER \
                                                                     : EXCEPTION_CONTINUE_SEARCH)
#define WXL_SEH_CODE() GetExceptionCode()

#elif WXL_ARCH_X86

namespace wxl::seh::x86
{
    /// One registration on the thread's handler chain. Its first two fields are the
    /// EXCEPTION_REGISTRATION_RECORD the dispatcher walks; the rest is ours.
    struct Frame
    {
        Frame*          prev;
        void*           handler;
        Catch           what;
        volatile DWORD  code;
        volatile bool   linked;
        void*           jump[5];   // __builtin_setjmp buffer

        /// Links the frame; the guard's __builtin_setjmp follows at once.
        explicit Frame(Catch c) noexcept
            : prev(reinterpret_cast<Frame*>(__readfsdword(0))), handler(reinterpret_cast<void*>(&Handle)),
              what(c), code(0), linked(true)
        {
            __writefsdword(0, reinterpret_cast<DWORD>(this));
        }
        ~Frame() { Unlink(); }
        Frame(const Frame&)            = delete;
        Frame& operator=(const Frame&) = delete;

        void Unlink() noexcept
        {
            if (!linked) return;
            linked = false;
            __writefsdword(0, reinterpret_cast<DWORD>(prev));
        }

        /// The dispatcher calls this on the first pass, innermost frame first, exactly as it would an
        /// __except filter. Accepting unwinds the frames below ours, drops ours, and lands in the handler.
        static EXCEPTION_DISPOSITION __cdecl Handle(EXCEPTION_RECORD* record, void* establisher, CONTEXT*, void*)
        {
            if (record->ExceptionFlags & (EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND)) return ExceptionContinueSearch;
            Frame* frame = static_cast<Frame*>(establisher);
            if (!Accepts(frame->what, record->ExceptionCode)) return ExceptionContinueSearch;
            frame->code = record->ExceptionCode;
            RtlUnwind(frame, nullptr, record, nullptr);
            frame->Unlink();
            __builtin_longjmp(frame->jump, 1);
        }
    };
}

#define WXL_SEH_TRY(what) \
    if (::wxl::seh::x86::Frame wxl_seh_frame(what); __builtin_setjmp(wxl_seh_frame.jump) == 0)
#define WXL_SEH_EXCEPT else
#define WXL_SEH_CODE() (wxl_seh_frame.code)

#else
#error "WXL_SEH_TRY: no structured exception support for this toolchain"
#endif

// =====================================================================================================
// Small helpers
// =====================================================================================================
namespace wxl
{
    /// A struct that mirrors client memory is pinned to its size at compile time.
    template <class T, std::size_t N>
    inline constexpr bool SizeIs = sizeof(T) == N;

    /// The integer behind an enum class, for flags and client fields.
    template <class E>
    constexpr std::underlying_type_t<E> ToUnderlying(E e) noexcept
    {
        return static_cast<std::underlying_type_t<E>>(e);
    }
}

#define WXL_STATIC_ASSERT_SIZE(type, size) \
    static_assert(::wxl::SizeIs<type, size>, #type " must be " #size " bytes to match the client")
