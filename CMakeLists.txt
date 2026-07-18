# WarcraftXL. Two artifact families share this tree and cannot share one toolchain platform, so the file
# is platform-aware: configure -A Win32 for the injected DLL + patcher + proxy (32-bit), and -A x64 with
# -DWXL_BUILD_HOST=ON for the asset host (64-bit). build.ps1 drives both.
# Copyright (C) 2026 WarcraftXL. GPLv3 (see source headers).

cmake_minimum_required(VERSION 3.20)
project(WarcraftXL LANGUAGES CXX)

# Static CRT everywhere: the DLLs load inside the player's Wow.exe and the host runs standalone on
# machines that have no reason to carry the toolset's brand-new VC++ redistributable. A dynamic CRT
# turns a missing/stale redist into a loader failure at process start (0xc0000142) with no log at all.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# The 64-bit asset host (WarcraftXLHost.exe) is opt-in: OFF by default so a contributor who only wants the
# DLL needs no x64 toolchain and pays no build/deploy cost. build.ps1 -Host flips it on in an x64 configure.
option(WXL_BUILD_HOST "Build WarcraftXLHost.exe (64-bit asset host)" OFF)

# wxl-modern-assets per-format toggles: exclude an entire format pipeline (source down-convert, both its
# DLL and host halves) at configure time, without touching source. common/ and textures/dds stay in
# unconditionally (shared infrastructure, not format-specific).
option(WXL_MODERN_M2  "Build wxl-modern-assets M2 support"          ON)
option(WXL_MODERN_M3  "Build wxl-modern-assets M3 support"          ON)
option(WXL_MODERN_WMO "Build wxl-modern-assets WMO support"         ON)
option(WXL_MODERN_BLP "Build wxl-modern-assets BLP/texture support" ON)
option(WXL_MODERN_RENDER
       "Build the D3D9On12 post-processing renderer"
       ON)
option(WXL_EXPERIMENTAL_WATER
       "Build the unmasked screen-space water reflection experiment"
       OFF)

set(WXL_MODERN_ASSETS_EXCLUDE_REGEX "")
if(NOT WXL_MODERN_M2)
    string(APPEND WXL_MODERN_ASSETS_EXCLUDE_REGEX "|/wxl-modern-assets/(shared|host|src)/models/m2/")
endif()
if(NOT WXL_MODERN_M3)
    string(APPEND WXL_MODERN_ASSETS_EXCLUDE_REGEX "|/wxl-modern-assets/(shared|host|src)/models/m3/")
endif()
if(NOT WXL_MODERN_WMO)
    string(APPEND WXL_MODERN_ASSETS_EXCLUDE_REGEX "|/wxl-modern-assets/(shared|host|src)/models/wmo/")
endif()
if(NOT WXL_MODERN_BLP)
    string(APPEND WXL_MODERN_ASSETS_EXCLUDE_REGEX "|/wxl-modern-assets/(shared|host|src)/textures/blp/")
endif()
string(REGEX REPLACE "^\\|" "" WXL_MODERN_ASSETS_EXCLUDE_REGEX "${WXL_MODERN_ASSETS_EXCLUDE_REGEX}")

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
    list(FILTER WXL_CORE_SRC EXCLUDE REGEX "/src/(patcher|host|gpu)/|/src/engine/gpu/")

    # wxl-modern-render consumes the richer pre-v1.1 render boundary (SSAA/readable depth/Glue post-FX).
    # Its compatibility implementation owns the same render hooks as the new split feature, so compile one
    # implementation or the other—never both.
    if(WXL_MODERN_RENDER)
        list(FILTER WXL_CORE_SRC EXCLUDE REGEX
             "/src/features/render/(Render|M2Draw)\\.cpp$")
    else()
        list(FILTER WXL_CORE_SRC EXCLUDE REGEX "/src/runtime/RenderHooks\\.cpp$")
    endif()

    # wxl-host-extension supplies the richer DB2 snapshot implementation used by the runtime. Do not also
    # compile the core copies or the two implementations define the same symbols in WarcraftXL.dll.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/scripts/wxl-host-extension/src/db2/Db2.cpp")
        list(FILTER WXL_CORE_SRC EXCLUDE REGEX
             "/src/engine/assets/db2/(Db2|ItemDisplayIndex|Wdc5)\\.cpp$")
    endif()

    # =========================================================================
    # Target 1: WarcraftXL.dll  (32-bit, injected) = engine + features + runtime scripts
    #   runtime scripts live in scripts/*/src/*.cpp; the cross-target byte-transform a module shares with
    #   the host lives in scripts/*/shared/*.cpp. Both compile straight into the DLL (self-registering).
    # =========================================================================
    file(GLOB_RECURSE WXL_RUNTIME_SCRIPT CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/*/src/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/*/shared/*.cpp")
    if(WXL_MODERN_ASSETS_EXCLUDE_REGEX)
        list(FILTER WXL_RUNTIME_SCRIPT EXCLUDE REGEX "${WXL_MODERN_ASSETS_EXCLUDE_REGEX}")
    endif()
    if(NOT WXL_EXPERIMENTAL_WATER)
        list(FILTER WXL_RUNTIME_SCRIPT EXCLUDE REGEX "/wxl-experimental-water/")
    endif()
    if(NOT WXL_MODERN_RENDER)
        list(FILTER WXL_RUNTIME_SCRIPT EXCLUDE REGEX "/wxl-modern-render/")
    endif()

    # Vendored ImGui (core + dx9/win32 backends). Compiled in but DORMANT until a feature wires it up.
    file(GLOB WXL_IMGUI CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/deps/imgui/*.cpp")

    # FlatBuffers runtime (flexbuffers string utils) for the host IPC client (engine/storage).
    set(WXL_FLATBUFFERS "${CMAKE_CURRENT_SOURCE_DIR}/deps/flatbuffers/src/util.cpp")

    add_library(WarcraftXL SHARED ${WXL_CORE_SRC} ${WXL_RUNTIME_SCRIPT} ${WXL_IMGUI} ${WXL_FLATBUFFERS})
    set_target_properties(WarcraftXL PROPERTIES OUTPUT_NAME "WarcraftXL" PREFIX "")
    target_include_directories(WarcraftXL PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts"
        "${WXL_LJ_BUILD}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/deps/minhook/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/deps/imgui"
        "${CMAKE_CURRENT_SOURCE_DIR}/deps/flatbuffers/include")

    add_subdirectory(deps/minhook)
    add_dependencies(WarcraftXL luajit_build)
    target_link_libraries(WarcraftXL PRIVATE minhook "${WXL_LJ_LIB}" d3d9 shell32)
    if(WXL_MODERN_RENDER)
        target_link_libraries(WarcraftXL PRIVATE d3dcompiler delayimp)
        target_link_options(WarcraftXL PRIVATE
            "/DELAYLOAD:d3d12.dll"
            "/DELAYLOAD:d3dcompiler_47.dll")
    endif()
    target_compile_definitions(WarcraftXL PRIVATE ${WXL_DEFS})

    source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${WXL_CORE_SRC} ${WXL_RUNTIME_SCRIPT})

    # Deploy: copy the freshly linked DLL (and the witness extension) into the client folder.
    if(CLIENT_PATH)
        add_custom_command(TARGET WarcraftXL POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:WarcraftXL>" "${CLIENT_PATH}/WarcraftXL.dll"
            COMMENT "Deploy WarcraftXL.dll -> ${CLIENT_PATH}")
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/docs/extensions-example/hello.lua")
            add_custom_command(TARGET WarcraftXL POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "${CLIENT_PATH}/extensions"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${CMAKE_CURRENT_SOURCE_DIR}/docs/extensions-example/hello.lua"
                        "${CLIENT_PATH}/extensions/hello.lua"
                COMMENT "Deploy extensions/hello.lua -> ${CLIENT_PATH}")
        endif()
    endif()

    # Per-module CMake hook: a module may ship scripts/<m>/module.cmake to extend the WarcraftXL target
    # (add a vendored library, extra include dirs, link libraries). The file runs with the target defined
    # and ${CMAKE_CURRENT_LIST_DIR} pointing at the module folder. Optional; most modules need none.
    file(GLOB WXL_MODULE_CMAKES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/scripts/*/module.cmake")
    foreach(_wxl_module ${WXL_MODULE_CMAKES})
        if(NOT WXL_MODERN_RENDER AND _wxl_module MATCHES "/wxl-modern-render/module\\.cmake$")
            continue()
        endif()
        include("${_wxl_module}")
    endforeach()

    # =========================================================================
    # Target 2: wxl-patcher.exe  = the offline PE patcher + patcher scripts
    # =========================================================================
    file(GLOB_RECURSE WXL_PATCHER_CORE   CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/patcher/*.cpp")
    file(GLOB_RECURSE WXL_PATCHER_SCRIPT CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/scripts/*/patcher/*.cpp")

    # src/common/ compiles into every binary (leveled logger, config, shared helpers): the patcher and
    # proxy do not link the engine, and the host is another architecture, so the shared infrastructure
    # is object-level, not a link dependency.
    file(GLOB WXL_COMMON_SRC CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/common/*.cpp")

    add_executable(wxl-patcher ${WXL_PATCHER_CORE} ${WXL_PATCHER_SCRIPT} ${WXL_COMMON_SRC})
    set_target_properties(wxl-patcher PROPERTIES OUTPUT_NAME "wxl-patcher")
    target_include_directories(wxl-patcher PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(wxl-patcher PRIVATE ${WXL_DEFS})

    # =========================================================================
    # Target 3: d3d9.dll. The modern renderer needs the D3D9On12 proxy and its shared-device exports;
    # otherwise use the upstream native D3D9 passthrough.
    # =========================================================================
    if(WXL_MODERN_RENDER)
        file(GLOB WXL_GPU_SRC CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/*.cpp")
        set(WXL_GPU_DEF "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/Proxy.def")
    else()
        file(GLOB WXL_GPU_SRC CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/gpu/*.cpp")
        set(WXL_GPU_DEF "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/gpu/Proxy.def")
    endif()

    add_library(d3d9 SHARED ${WXL_GPU_SRC} ${WXL_COMMON_SRC})
    set_target_properties(d3d9 PROPERTIES OUTPUT_NAME "d3d9" PREFIX "")
    target_include_directories(d3d9 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_options(d3d9 PRIVATE "/DEF:${WXL_GPU_DEF}")
    if(WXL_MODERN_RENDER)
        target_link_libraries(d3d9 PRIVATE d3d12 dxgi dcomp d3dcompiler delayimp)
        # Keep optional platform components delay-loaded: the proxy falls back to native D3D9 when On12
        # is unavailable instead of preventing Wow.exe from starting.
        target_link_options(d3d9 PRIVATE
            "/DELAYLOAD:d3d12.dll"
            "/DELAYLOAD:dxgi.dll"
            "/DELAYLOAD:dcomp.dll"
            "/DELAYLOAD:d3dcompiler_47.dll")
    endif()
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
#   Format-blind: src/host/ + the modules' scripts/*/host/ handlers + their scripts/*/shared/ transforms.
#   StormLib + FlatBuffers (deps/). Requires an x64 toolchain.
# =============================================================================
if(WXL_BUILD_HOST AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    # StormLib (vendored) supplies MPQ reading. Static, read-only use; no install/tests.
    set(STORM_SKIP_INSTALL ON CACHE BOOL "" FORCE)
    set(STORM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/deps/stormlib" "${CMAKE_BINARY_DIR}/stormlib")

    file(GLOB_RECURSE WXL_HOST_CORE   CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/host/*.cpp")
    file(GLOB_RECURSE WXL_HOST_SCRIPT CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/*/host/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/*/shared/*.cpp")
    if(WXL_MODERN_ASSETS_EXCLUDE_REGEX)
        list(FILTER WXL_HOST_SCRIPT EXCLUDE REGEX "${WXL_MODERN_ASSETS_EXCLUDE_REGEX}")
    endif()

    # The shared infrastructure (leveled logger, config) compiles straight into the host too.
    file(GLOB WXL_COMMON_SRC CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/common/*.cpp")

    add_executable(WarcraftXLHost
        ${WXL_HOST_CORE}
        ${WXL_HOST_SCRIPT}
        ${WXL_COMMON_SRC}
        "${CMAKE_CURRENT_SOURCE_DIR}/deps/flatbuffers/src/util.cpp")
    set_target_properties(WarcraftXLHost PROPERTIES OUTPUT_NAME "WarcraftXLHost")
    target_include_directories(WarcraftXLHost PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/host"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts"
        "${CMAKE_CURRENT_SOURCE_DIR}/deps/flatbuffers/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/deps/stormlib/src")
    target_link_libraries(WarcraftXLHost PRIVATE storm)
    target_compile_definitions(WarcraftXLHost PRIVATE ${WXL_DEFS} WXL_HOST=1)

    source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${WXL_HOST_CORE} ${WXL_HOST_SCRIPT})

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
