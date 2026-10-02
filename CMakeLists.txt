# WarcraftXL. Everything builds Win32: the target client is a 32-bit process, and every archive an
# extension needs -- named, loose-directory, or a Patch-* the client's own patch-name scan finds --
# mounts and reads natively. The build is split by concern: cmake/ holds the settings and helpers,
# deps/ the vendored libraries, src/ the three binaries, extensions/ one DLL per folder.
# Copyright (C) 2026 WarcraftXL. GPLv3 (see source headers).

cmake_minimum_required(VERSION 3.20)
# C is enabled for the vendored C libraries pulled in via add_subdirectory (MinHook).
project(WarcraftXL LANGUAGES CXX C)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(WxlDefaults)       # standard, static runtime, WXL_DEFS, CLIENT_PATH
include(WxlDeploy)         # wxl_deploy(): copies a built target into the client folder
include(WxlSdkBoundary)    # an extension must not include offsets/ directly
include(WxlExtensions)     # wxl_add_extensions(): one DLL per folder

# Everything here is Win32-only; a 64-bit configure builds nothing.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(WARNING "x64 configure builds nothing here; configure -A Win32.")
    return()
endif()

add_subdirectory(deps)   # minhook, Dear ImGui
add_subdirectory(src)    # WarcraftXL.dll, wxl-patcher.exe, d3d9.dll
wxl_add_extensions("${CMAKE_CURRENT_SOURCE_DIR}/extensions")
