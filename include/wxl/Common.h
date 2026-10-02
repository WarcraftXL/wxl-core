// The toolchain-neutral layer: one spelling for what MSVC, clang and GCC each write differently.
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

#ifndef WXL_COMMON_H
#define WXL_COMMON_H

// Plain C, so PluginApi.h and any C extension can include it. The C++ half (the SEH guard and the
// helpers that need a compiler) is wxl/Common.hpp, which includes this file.
//
// The client is a Windows program: the target is always Windows, whatever the build host (Visual
// Studio on Windows, clang mingw-w64 on Linux). Every macro below is about the compiler, never the
// host.

// --- Platform and architecture --------------------------------------------------------------------
#if defined(_WIN32)
#define WXL_PLATFORM_WINDOWS 1
#else
#error "WarcraftXL targets the Windows client; build with a Windows toolchain (MSVC or mingw-w64)"
#endif

#if defined(_WIN64) || defined(__x86_64__)
#define WXL_ARCH_X64 1
#define WXL_ARCH_X86 0
#else
#define WXL_ARCH_X64 0
#define WXL_ARCH_X86 1
#endif

// --- Compiler -------------------------------------------------------------------------------------
// Exactly one WXL_COMPILER_* is 1. clang-cl counts as clang with the MSVC front-end: it takes MSVC's
// flags, keywords and runtime, so WXL_MSVC_FRONTEND covers it too. WXL_MINGW is any GNU-ABI build.
#if defined(__clang__)
#define WXL_COMPILER_CLANG 1
#define WXL_COMPILER_GCC   0
#define WXL_COMPILER_MSVC  0
#elif defined(__GNUC__)
#define WXL_COMPILER_CLANG 0
#define WXL_COMPILER_GCC   1
#define WXL_COMPILER_MSVC  0
#elif defined(_MSC_VER)
#define WXL_COMPILER_CLANG 0
#define WXL_COMPILER_GCC   0
#define WXL_COMPILER_MSVC  1
#else
#error "unsupported compiler: MSVC, clang or GCC"
#endif

#if defined(_MSC_VER)
#define WXL_MSVC_FRONTEND 1
#else
#define WXL_MSVC_FRONTEND 0
#endif

#if defined(__MINGW32__) || defined(__MINGW64__)
#define WXL_MINGW 1
#else
#define WXL_MINGW 0
#endif

#if WXL_COMPILER_MSVC
#define WXL_COMPILER_NAME "msvc"
#elif WXL_COMPILER_CLANG && WXL_MSVC_FRONTEND
#define WXL_COMPILER_NAME "clang-cl"
#elif WXL_COMPILER_CLANG
#define WXL_COMPILER_NAME "clang"
#else
#define WXL_COMPILER_NAME "gcc"
#endif

// --- Preprocessor helpers -------------------------------------------------------------------------
#define WXL_STRINGIFY_(x) #x
#define WXL_STRINGIFY(x)  WXL_STRINGIFY_(x)
#define WXL_CONCAT_(a, b) a##b
#define WXL_CONCAT(a, b)  WXL_CONCAT_(a, b)

#if WXL_MSVC_FRONTEND
#define WXL_PRAGMA(x) __pragma(x)
#else
#define WXL_PRAGMA(x) _Pragma(#x)
#endif

// --- Linkage --------------------------------------------------------------------------------------
// Every Windows compiler understands __declspec(dllexport); mingw maps it to the GNU attribute.
#define WXL_EXPORT __declspec(dllexport)
#define WXL_IMPORT __declspec(dllimport)

#if defined(__cplusplus)
#define WXL_EXTERN_C       extern "C"
#define WXL_EXTERN_C_BEGIN extern "C" {
#define WXL_EXTERN_C_END   }
#else
#define WXL_EXTERN_C
#define WXL_EXTERN_C_BEGIN
#define WXL_EXTERN_C_END
#endif

// --- Calling conventions --------------------------------------------------------------------------
// The client is x86, where the convention is part of every signature. x64 has a single convention
// and the keywords are ignored there, with a warning on some compilers: they become nothing.
#if WXL_ARCH_X86
#define WXL_CDECL    __cdecl
#define WXL_STDCALL  __stdcall
#define WXL_THISCALL __thiscall
#define WXL_FASTCALL __fastcall
#else
#define WXL_CDECL
#define WXL_STDCALL
#define WXL_THISCALL
#define WXL_FASTCALL
#endif

// --- Function attributes --------------------------------------------------------------------------
#if WXL_MSVC_FRONTEND
#define WXL_FORCEINLINE __forceinline
#define WXL_NOINLINE    __declspec(noinline)
#define WXL_NAKED       __declspec(naked)
#define WXL_NORETURN    __declspec(noreturn)
#define WXL_RESTRICT    __restrict
#else
#define WXL_FORCEINLINE inline __attribute__((always_inline))
#define WXL_NOINLINE    __attribute__((noinline))
#define WXL_NAKED       __attribute__((naked))
#define WXL_NORETURN    __attribute__((noreturn))
#define WXL_RESTRICT    __restrict__
#endif

// --- Hints to the optimiser -----------------------------------------------------------------------
#if WXL_COMPILER_MSVC
#define WXL_LIKELY(x)       (x)
#define WXL_UNLIKELY(x)     (x)
#define WXL_ASSUME(x)       __assume(x)
#define WXL_UNREACHABLE()   __assume(0)
#else
#define WXL_LIKELY(x)       __builtin_expect(!!(x), 1)
#define WXL_UNLIKELY(x)     __builtin_expect(!!(x), 0)
#define WXL_ASSUME(x)       do { if (!(x)) __builtin_unreachable(); } while (0)
#define WXL_UNREACHABLE()   __builtin_unreachable()
#endif

// --- Intrinsics -----------------------------------------------------------------------------------
// WXL_RETURN_ADDRESS(): the address the current function returns to; a hook uses it to name its
// caller. WXL_ADDRESS_OF_RETURN_ADDRESS(): where that address sits on the stack. The latter needs a
// frame pointer under GCC (see WXL_KEEP_FRAME_POINTER); MSVC and clang provide it as an intrinsic.
#if WXL_MSVC_FRONTEND
#include <intrin.h>
#pragma intrinsic(_ReturnAddress)
#pragma intrinsic(_AddressOfReturnAddress)
#define WXL_RETURN_ADDRESS()            _ReturnAddress()
#define WXL_ADDRESS_OF_RETURN_ADDRESS() _AddressOfReturnAddress()
#define WXL_DEBUGBREAK()                __debugbreak()
#elif WXL_COMPILER_CLANG
#include <intrin.h>
#define WXL_RETURN_ADDRESS()            __builtin_return_address(0)
#define WXL_ADDRESS_OF_RETURN_ADDRESS() _AddressOfReturnAddress()
#define WXL_DEBUGBREAK()                __builtin_debugtrap()
#else
#include <intrin.h>
#define WXL_RETURN_ADDRESS()            __builtin_return_address(0)
#define WXL_ADDRESS_OF_RETURN_ADDRESS() ((void*)((char*)__builtin_frame_address(0) + sizeof(void*)))
#define WXL_DEBUGBREAK()                __asm__ __volatile__("int3")
#endif

// --- Code generation per function -----------------------------------------------------------------
// WXL_OPTIMIZE_OFF / WXL_OPTIMIZE_ON bracket functions that must compile as written (a hook whose
// prologue is inspected, a timing loop). WXL_KEEP_FRAME_POINTER marks a function whose caller's EBP
// is read through its own frame: MSVC keeps frames with "y" off, GCC with the attribute, clang only
// per file (-fno-omit-frame-pointer, set in CMake on the files that need it).
#if WXL_COMPILER_MSVC
#define WXL_OPTIMIZE_OFF       __pragma(optimize("", off))
#define WXL_OPTIMIZE_ON        __pragma(optimize("", on))
#define WXL_KEEP_FRAME_POINTER
#elif WXL_COMPILER_CLANG
#define WXL_OPTIMIZE_OFF       _Pragma("clang optimize off")
#define WXL_OPTIMIZE_ON        _Pragma("clang optimize on")
#define WXL_KEEP_FRAME_POINTER
#else
#define WXL_OPTIMIZE_OFF       _Pragma("GCC push_options") _Pragma("GCC optimize(\"O0\")")
#define WXL_OPTIMIZE_ON        _Pragma("GCC pop_options")
#define WXL_KEEP_FRAME_POINTER __attribute__((optimize("no-omit-frame-pointer")))
#endif

// --- Layout ---------------------------------------------------------------------------------------
// WXL_PACK_PUSH(1) ... WXL_PACK_POP around a struct that mirrors client memory byte for byte.
#define WXL_PACK_PUSH(n) WXL_PRAGMA(pack(push, n))
#define WXL_PACK_POP     WXL_PRAGMA(pack(pop))

#if defined(__cplusplus)
#define WXL_ALIGNAS(n)     alignas(n)
#define WXL_THREAD_LOCAL   thread_local
#elif WXL_MSVC_FRONTEND
#define WXL_ALIGNAS(n)     __declspec(align(n))
#define WXL_THREAD_LOCAL   __declspec(thread)
#else
#define WXL_ALIGNAS(n)     __attribute__((aligned(n)))
#define WXL_THREAD_LOCAL   __thread
#endif

// --- Diagnostics ----------------------------------------------------------------------------------
// WXL_FUNCTION_SIGNATURE: the full signature as a string, for logs.
// WXL_WARNINGS_PUSH / WXL_WARNINGS_POP bracket a third-party or client header; the
// WXL_WARNING_DISABLE_* lines in between name a warning per compiler and are nothing on the others.
#if WXL_MSVC_FRONTEND
#define WXL_FUNCTION_SIGNATURE __FUNCSIG__
#else
#define WXL_FUNCTION_SIGNATURE __PRETTY_FUNCTION__
#endif

#if WXL_COMPILER_MSVC
#define WXL_WARNINGS_PUSH             __pragma(warning(push))
#define WXL_WARNINGS_POP              __pragma(warning(pop))
#define WXL_WARNING_DISABLE_MSVC(n)   __pragma(warning(disable : n))
#define WXL_WARNING_DISABLE_CLANG(w)
#define WXL_WARNING_DISABLE_GCC(w)
#elif WXL_COMPILER_CLANG
#define WXL_WARNINGS_PUSH             _Pragma("clang diagnostic push")
#define WXL_WARNINGS_POP              _Pragma("clang diagnostic pop")
#define WXL_WARNING_DISABLE_MSVC(n)
#define WXL_WARNING_DISABLE_CLANG(w)  WXL_PRAGMA(clang diagnostic ignored w)
#define WXL_WARNING_DISABLE_GCC(w)
#else
#define WXL_WARNINGS_PUSH             _Pragma("GCC diagnostic push")
#define WXL_WARNINGS_POP              _Pragma("GCC diagnostic pop")
#define WXL_WARNING_DISABLE_MSVC(n)
#define WXL_WARNING_DISABLE_CLANG(w)
#define WXL_WARNING_DISABLE_GCC(w)    WXL_PRAGMA(GCC diagnostic ignored w)
#endif

// --- COM identities -------------------------------------------------------------------------------
// __uuidof(T) works on every compiler once both lines are given: MSVC reads the attribute on the
// type, mingw the declaration that follows the type at global scope.
//   struct WXL_UUID_ATTR("3461a81b-ce41-485b-b6b5-fcf08ba6a6bd") IFoo : IUnknown { ... };
//   WXL_UUID_DECL(IFoo, 0x3461a81b, 0xce41, 0x485b, 0xb6, 0xb5, 0xfc, 0xf0, 0x8b, 0xa6, 0xa6, 0xbd)
#if WXL_MSVC_FRONTEND
#define WXL_UUID_ATTR(str) __declspec(uuid(str))
#define WXL_UUID_DECL(type, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8)
#elif defined(__CRT_UUID_DECL)
#define WXL_UUID_ATTR(str)
#define WXL_UUID_DECL(type, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
    __CRT_UUID_DECL(type, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8)
#else
#define WXL_UUID_ATTR(str)
#define WXL_UUID_DECL(type, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8)
#endif

#endif // WXL_COMMON_H
