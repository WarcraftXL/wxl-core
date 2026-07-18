# WarcraftXL. Two artifact families share this tree and cannot share one toolchain platform, so the file
# is platform-aware: configure -A Win32 for the injected DLL + patcher + proxy (32-bit), and -A x64 with
# -DWXL_BUILD_HOST=ON for the asset host (64-bit). build.ps1 drives both.
# Copyright (C) 2026 WarcraftXL. GPLv3 (see source headers).

cmake_minimum_required(VERSION 3.20)
# C is enabled for the vendored TweetNaCl (deps/tweetnacl/tweetnacl.c): the Ed25519 + SHA-512 primitives
# behind extension signing. It compiles into the DLL (verify + hash) and the wxl-sign tool (keygen + sign).
project(WarcraftXL LANGUAGES CXX C)

# Static CRT everywhere: the DLLs load inside the player's Wow.exe and the host runs standalone on
# machines that have no reason to carry the toolset's brand-new VC++ redistributable. A dynamic CRT
# turns a missing/stale redist into a loader failure at process start (0xc0000142) with no log at all.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# The 64-bit asset host (WarcraftXLHost.exe) is opt-in: OFF by default so a contributor who only wants the
# DLL needs no x64 toolchain and pays no build/deploy cost. build.ps1 -Host flips it on in an x64 configure.
option(WXL_BUILD_HOST "Build WarcraftXLHost.exe (64-bit asset host)" OFF)

# The modern-assets pipelines (ex wxl-modern-assets, now src/engine/assets/) are always compiled; their
# per-format enablement is a runtime/compile switch in src/config.hpp (wxl::features::kModern*), honored by
# each module's registrar via `if constexpr`. No CMake exclusion — see config.hpp.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # feeds the IDE the real C++20 flags

set(WXL_DEFS WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)

# Client directory to deploy the built artifacts into. Passed by build.ps1 (-DCLIENT_PATH=...) and cached.
set(CLIENT_PATH "" CACHE PATH "Client folder the built artifacts are copied into after each build")

# =============================================================================
# 32-bit family (injected into the client): WarcraftXL.dll + wxl-patcher.exe + d3d9.dll
# =============================================================================
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    # -------------------------------------------------------------------------
    # LuaJIT, static (x86, /MT), linked INTO WarcraftXL.dll: no luajit.dll/lua51.dll ships to the
    # player. LuaJIT bootstraps its own code generator (minilua -> buildvm -> lj_vm.obj), so the
    # vendored msvcbuild.bat drives the build from a 32-bit VS shell; a patched COPY forces /MT to
    # match the project-wide static CRT. See deps/luajit/WXL-VENDOR.md.
    # -------------------------------------------------------------------------
    set(WXL_VCVARSALL "" CACHE FILEPATH "vcvarsall.bat used to build LuaJIT for x86")
    if(NOT WXL_VCVARSALL)
        file(GLOB _wxl_vcvars
            "C:/Program Files/Microsoft Visual Studio/*/*/VC/Auxiliary/Build/vcvarsall.bat"
            "C:/Program Files (x86)/Microsoft Visual Studio/*/*/VC/Auxiliary/Build/vcvarsall.bat")
        if(_wxl_vcvars)
            list(GET _wxl_vcvars 0 WXL_VCVARSALL)
        endif()
    endif()
    if(NOT WXL_VCVARSALL)
        message(FATAL_ERROR "vcvarsall.bat not found; pass -DWXL_VCVARSALL=<path> to build LuaJIT.")
    endif()

    set(WXL_LJ_SRC   "${CMAKE_CURRENT_SOURCE_DIR}/deps/luajit")
    set(WXL_LJ_BUILD "${CMAKE_BINARY_DIR}/luajit")
    set(WXL_LJ_LIB   "${WXL_LJ_BUILD}/src/lua51.lib")

    file(TO_NATIVE_PATH "${WXL_VCVARSALL}"      WXL_VCVARSALL_NATIVE)
    file(TO_NATIVE_PATH "${WXL_LJ_BUILD}/src"   WXL_LJ_BUILD_SRC_NATIVE)
    # The VS dev environment sets NoDefaultCurrentDirectoryInExePath, which breaks msvcbuild.bat's
    # bare invocations of its own bootstrap tools (minilua, buildvm) from the working directory.
    file(WRITE "${CMAKE_BINARY_DIR}/wxl-build-luajit.bat"
"@echo off\r
call \"${WXL_VCVARSALL_NATIVE}\" x86\r
if errorlevel 1 exit /b 1\r
set NoDefaultCurrentDirectoryInExePath=\r
cd /d \"${WXL_LJ_BUILD_SRC_NATIVE}\"\r
if errorlevel 1 exit /b 1\r
call \"${WXL_LJ_BUILD_SRC_NATIVE}\\msvcbuild.bat\" static\r
")

    add_custom_command(
        OUTPUT "${WXL_LJ_LIB}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${WXL_LJ_SRC}" "${WXL_LJ_BUILD}"
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -Command
                "(Get-Content -Raw '${WXL_LJ_BUILD}/src/msvcbuild.bat') -replace '/W3 /D_CRT_SECURE_NO_DEPRECATE','/W3 /MT /D_CRT_SECURE_NO_DEPRECATE' | Set-Content '${WXL_LJ_BUILD}/src/msvcbuild.bat'"
        COMMAND cmd /c "${CMAKE_BINARY_DIR}/wxl-build-luajit.bat"
        COMMENT "Building LuaJIT static (x86, /MT)"
        VERBATIM)
    add_custom_target(luajit_build DEPENDS "${WXL_LJ_LIB}")

    # --- Automatic, recursive source discovery (no hand-written paths) -----------------------------
    # CONFIGURE_DEPENDS re-globs at build time, so dropping a file (or a whole script) needs no edit here.
    # The DLL excludes the patcher/host trees and the proxy (engine/gpu builds its own target below).
    file(GLOB_RECURSE WXL_CORE_SRC CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
    list(FILTER WXL_CORE_SRC EXCLUDE REGEX "/src/(patcher|host|sign)/|/src/engine/gpu/")

    # Vendored TweetNaCl: Ed25519 (crypto_sign_open) + SHA-512 (crypto_hash), public domain, no deps.
    # A .c file, so it is not caught by the *.cpp glob above and is listed explicitly.
    set(WXL_TWEETNACL "${CMAKE_CURRENT_SOURCE_DIR}/deps/tweetnacl/tweetnacl.c")

    # =========================================================================
    # Target 1: WarcraftXL.dll  (32-bit, injected) = engine + features + assets pipelines
    #   The modern-assets DLL + cross-target-shared halves now live in src/engine/assets/ and are caught
    #   by the src/*.cpp glob above (the shared half also compiles into the 64-bit host, added there).
    # =========================================================================
    # Vendored ImGui (core + dx9/win32 backends). Compiled in but DORMANT until a feature wires it up.
    file(GLOB WXL_IMGUI CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/deps/imgui/*.cpp")

    # FlatBuffers runtime (flexbuffers string utils) for the host IPC client (engine/storage).
    set(WXL_FLATBUFFERS "${CMAKE_CURRENT_SOURCE_DIR}/deps/flatbuffers/src/util.cpp")

    add_library(WarcraftXL SHARED ${WXL_CORE_SRC} ${WXL_IMGUI} ${WXL_FLATBUFFERS} ${WXL_TWEETNACL})
    set_target_properties(WarcraftXL PROPERTIES OUTPUT_NAME "WarcraftXL" PREFIX "")
    target_include_directories(WarcraftXL PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${WXL_LJ_BUILD}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/deps/minhook/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/deps/imgui"
        "${CMAKE_CURRENT_SOURCE_DIR}/deps/flatbuffers/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/deps/tweetnacl")

    add_subdirectory(deps/minhook)
    add_dependencies(WarcraftXL luajit_build)
    target_link_libraries(WarcraftXL PRIVATE minhook "${WXL_LJ_LIB}" d3d9 shell32)
    target_compile_definitions(WarcraftXL PRIVATE ${WXL_DEFS})

    source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${WXL_CORE_SRC})

    # Deploy: copy the freshly linked DLL (and the witness extension) into the client folder.
    if(CLIENT_PATH)
        add_custom_command(TARGET WarcraftXL POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:WarcraftXL>" "${CLIENT_PATH}/WarcraftXL.dll"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CLIENT_PATH}/extensions"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_CURRENT_SOURCE_DIR}/docs/extensions-example/hello.lua"
                    "${CLIENT_PATH}/extensions/hello.lua"
            COMMENT "Deploy WarcraftXL.dll + extensions/hello.lua -> ${CLIENT_PATH}")
    endif()

    # =========================================================================
    # Target 2: wxl-patcher.exe  = the offline PE patcher (incl. the ex-wxl-no-gluexml example under
    #   src/patcher/, now caught by the src/patcher glob below).
    # =========================================================================
    file(GLOB_RECURSE WXL_PATCHER_CORE CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/patcher/*.cpp")

    # src/common/ compiles into every binary (leveled logger, config, shared helpers): the patcher and
    # proxy do not link the engine, and the host is another architecture, so the shared infrastructure
    # is object-level, not a link dependency.
    file(GLOB WXL_COMMON_SRC CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/common/*.cpp")

    add_executable(wxl-patcher ${WXL_PATCHER_CORE} ${WXL_COMMON_SRC})
    set_target_properties(wxl-patcher PROPERTIES OUTPUT_NAME "wxl-patcher")
    target_include_directories(wxl-patcher PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(wxl-patcher PRIVATE ${WXL_DEFS})

    # =========================================================================
    # Target 4: wxl-sign.exe  = offline Ed25519 keygen + extension manifest signer
    # =========================================================================
    file(GLOB WXL_SIGN_SRC CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/sign/*.cpp")

    add_executable(wxl-sign
        ${WXL_SIGN_SRC}
        "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/security/Crypto.cpp"
        ${WXL_COMMON_SRC}
        ${WXL_TWEETNACL})
    set_target_properties(wxl-sign PROPERTIES OUTPUT_NAME "wxl-sign")
    target_include_directories(wxl-sign PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/deps/tweetnacl"
        "${WXL_LJ_BUILD}/src")                 # generated luajit.h -> LUAJIT_VERSION
    add_dependencies(wxl-sign luajit_build)    # luajit.h is generated by the LuaJIT build
    target_compile_definitions(wxl-sign PRIVATE ${WXL_DEFS})

    # =========================================================================
    # Target 3: d3d9.dll  = the native proxy (system-d3d9 passthrough + runtime loader)
    # =========================================================================
    file(GLOB WXL_GPU_SRC CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/gpu/*.cpp")

    add_library(d3d9 SHARED ${WXL_GPU_SRC} ${WXL_COMMON_SRC})
    set_target_properties(d3d9 PROPERTIES OUTPUT_NAME "d3d9" PREFIX "")
    target_include_directories(d3d9 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    # The proxy shadows a static import of Wow.exe, so a hard dependency of the proxy is a hard
    # dependency of the game. It resolves the system d3d9 at runtime and links nothing beyond the CRT.
    target_link_options(d3d9 PRIVATE "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/src/engine/gpu/Proxy.def")
    target_compile_definitions(d3d9 PRIVATE ${WXL_DEFS})

    # Deploy: copy the freshly linked proxy into the client folder after every build.
    if(CLIENT_PATH)
        add_custom_command(TARGET d3d9 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:d3d9>" "${CLIENT_PATH}/d3d9.dll"
            COMMENT "Deploy d3d9.dll -> ${CLIENT_PATH}")
    endif()
endif()

# =============================================================================
# 64-bit family (opt-in): WarcraftXLHost.exe = archive owner + IPC transport.
#   Format-blind: src/host/ (incl. assets/ host handlers + resolver/) + the cross-target byte transforms
#   shared with the DLL in src/engine/assets/shared/. StormLib + FlatBuffers (deps/). Requires x64.
# =============================================================================
if(WXL_BUILD_HOST AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    # StormLib (vendored) supplies MPQ reading. Static, read-only use; no install/tests.
    set(STORM_SKIP_INSTALL ON CACHE BOOL "" FORCE)
    set(STORM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/deps/stormlib" "${CMAKE_BINARY_DIR}/stormlib")

    # src/host/ = host core + the ex-module host handlers (assets/, resolver/), all self-registering.
    file(GLOB_RECURSE WXL_HOST_CORE CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/host/*.cpp")
    # The cross-target byte transforms shared with the DLL (ex scripts/*/shared) now live here.
    file(GLOB_RECURSE WXL_HOST_SHARED CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/assets/shared/*.cpp")

    # The shared infrastructure (leveled logger, config) compiles straight into the host too.
    file(GLOB WXL_COMMON_SRC CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/common/*.cpp")

    add_executable(WarcraftXLHost
        ${WXL_HOST_CORE}
        ${WXL_HOST_SHARED}
        ${WXL_COMMON_SRC}
        "${CMAKE_CURRENT_SOURCE_DIR}/deps/flatbuffers/src/util.cpp")
    set_target_properties(WarcraftXLHost PROPERTIES OUTPUT_NAME "WarcraftXLHost")
    target_include_directories(WarcraftXLHost PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/host"
        "${CMAKE_CURRENT_SOURCE_DIR}/deps/flatbuffers/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/deps/stormlib/src")
    target_link_libraries(WarcraftXLHost PRIVATE storm)
    target_compile_definitions(WarcraftXLHost PRIVATE ${WXL_DEFS} WXL_HOST=1)

    source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${WXL_HOST_CORE} ${WXL_HOST_SHARED})

    # Deploy the host into the client's Utils folder after build.
    if(CLIENT_PATH)
        add_custom_command(TARGET WarcraftXLHost POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CLIENT_PATH}/Utils"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:WarcraftXLHost>" "${CLIENT_PATH}/Utils/WarcraftXLHost.exe"
            COMMENT "Deploy WarcraftXLHost.exe -> ${CLIENT_PATH}/Utils")
    endif()
endif()

# A 64-bit configure that did not ask for the host builds nothing; nudge toward the right flags.
if(CMAKE_SIZEOF_VOID_P EQUAL 8 AND NOT WXL_BUILD_HOST)
    message(WARNING "x64 configure without -DWXL_BUILD_HOST=ON builds nothing; the 32-bit family needs -A Win32.")
endif()
