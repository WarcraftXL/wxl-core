# Project-wide build settings: the language standard, the static runtime, the definitions every
# target compiles with, and the client folder the artifacts deploy into.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # feeds the IDE the real C++20 flags

# Static CRT: the DLLs load inside the player's Wow.exe, which has no reason to carry the toolset's
# brand-new VC++ redistributable. A dynamic CRT turns a missing/stale redist into a loader failure at
# process start (0xc0000142) with no log at all.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
# The mingw equivalent (cross-build from Linux, cmake/toolchain/): the C++ runtime, unwinder and
# pthread shim go in statically, so a binary depends on nothing but the system DLLs.
# -fms-extensions turns on the MSVC keywords the client code relies on (__try/__except,
# __declspec(uuid), __stdcall on function pointers).
if(MINGW)
    add_compile_options(-fms-extensions)
    add_link_options(-static)
endif()

set(WXL_DEFS WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)

# Every binary lands at the build root (build/<Config>/ under Visual Studio), whatever folder
# declares it: build.ps1 and the release workflow pick them up there.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# Client directory to deploy the built artifacts into. Passed by build.ps1 (-DCLIENT_PATH=...) and cached.
set(CLIENT_PATH "" CACHE PATH "Client folder the built artifacts are copied into after each build")
