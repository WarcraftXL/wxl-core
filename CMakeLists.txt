# WarcraftXL. Two artifact families share this tree and cannot share one toolchain platform, so the file
# is platform-aware: configure -A Win32 for the injected DLL + patcher + proxy (32-bit), and -A x64 with
# -DWXL_BUILD_HOST=ON for the asset host (64-bit). build.ps1 drives both.
# Copyright (C) 2026 WarcraftXL. GPLv3 (see source headers).

cmake_minimum_required(VERSION 3.20)
project(WarcraftXL LANGUAGES CXX)

# The 64-bit asset host (WarcraftXLHost.exe) is opt-in: OFF by default so a contributor who only wants the
# DLL needs no x64 toolchain and pays no build/deploy cost. build.ps1 -Host flips it on in an x64 configure.
option(WXL_BUILD_HOST "Build WarcraftXLHost.exe (64-bit asset host)" OFF)

# wxl-modern-assets per-format toggles: exclude an entire format pipeline (source down-convert, both its
# DLL and host halves) at configure time, without touching source. common/ and textures/dds stay in
# unconditionally (shared infrastructure, not format-specific). ADT gets WXL_MODERN_ADT here once it
# migrates into wxl-modern-assets.
option(WXL_MODERN_M2  "Build wxl-modern-assets M2 support"          ON)
option(WXL_MODERN_M3  "Build wxl-modern-assets M3 support"          ON)
option(WXL_MODERN_WMO "Build wxl-modern-assets WMO support"         ON)
option(WXL_MODERN_BLP "Build wxl-modern-assets BLP/texture support" ON)

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
    # --- Automatic, recursive source discovery (no hand-written paths) -----------------------------
    # CONFIGURE_DEPENDS re-globs at build time, so dropping a file (or a whole script) needs no edit here.
    # The DLL excludes the patcher/host/gpu trees; the host (64-bit) takes its own slice in the EQUAL 8 block.
    file(GLOB_RECURSE WXL_CORE_SRC CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
    list(FILTER WXL_CORE_SRC EXCLUDE REGEX "/src/(patcher|host|gpu)/")

    # =========================================================================
    # Target 1: WarcraftXL.dll  (32-bit, injected) = SDK + runtime scripts
    #   runtime scripts live in scripts/*/src/*.cpp; the cross-target byte-transform a module shares with
    #   the host lives in scripts/*/shared/*.cpp. Both compile straight into the DLL (self-registering).
    # =========================================================================
    file(GLOB_RECURSE WXL_RUNTIME_SCRIPT CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/*/src/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/*/shared/*.cpp")
    if(WXL_MODERN_ASSETS_EXCLUDE_REGEX)
        list(FILTER WXL_RUNTIME_SCRIPT EXCLUDE REGEX "${WXL_MODERN_ASSETS_EXCLUDE_REGEX}")
    endif()

    # Vendored ImGui (core + dx9/win32 backends). Compiled in but DORMANT until a script wires it up.
    file(GLOB WXL_IMGUI CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/deps/imgui/*.cpp")

    # FlatBuffers runtime (flexbuffers string utils) for the host IPC client (runtime/storage).
    set(WXL_FLATBUFFERS "${CMAKE_CURRENT_SOURCE_DIR}/deps/flatbuffers/src/util.cpp")

    add_library(WarcraftXL SHARED ${WXL_CORE_SRC} ${WXL_RUNTIME_SCRIPT} ${WXL_IMGUI} ${WXL_FLATBUFFERS})
    set_target_properties(WarcraftXL PROPERTIES OUTPUT_NAME "WarcraftXL" PREFIX "")
    target_include_directories(WarcraftXL PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/deps/minhook/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/deps/imgui"
        "${CMAKE_CURRENT_SOURCE_DIR}/deps/flatbuffers/include")

    add_subdirectory(deps/minhook)
    target_link_libraries(WarcraftXL PRIVATE minhook d3d9 d3dcompiler shell32)
    target_compile_definitions(WarcraftXL PRIVATE ${WXL_DEFS})

    source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${WXL_CORE_SRC} ${WXL_RUNTIME_SCRIPT})

    # Deploy: copy the freshly linked DLL into the client folder after every build.
    if(CLIENT_PATH)
        add_custom_command(TARGET WarcraftXL POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:WarcraftXL>" "${CLIENT_PATH}/WarcraftXL.dll"
            COMMENT "Deploy WarcraftXL.dll -> ${CLIENT_PATH}")
    endif()

    # Per-module CMake hook: a module may ship scripts/<m>/module.cmake to extend the WarcraftXL target
    # (add a vendored library, extra include dirs, link libraries). The file runs with the target defined
    # and ${CMAKE_CURRENT_LIST_DIR} pointing at the module folder. Optional; most modules need none.
    file(GLOB WXL_MODULE_CMAKES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/scripts/*/module.cmake")
    foreach(_wxl_module ${WXL_MODULE_CMAKES})
        include("${_wxl_module}")
    endforeach()

    # =========================================================================
    # Target 2: wxl-patcher.exe  = the offline PE patcher + patcher scripts
    # =========================================================================
    file(GLOB        WXL_PATCHER_CORE   CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/patcher/*.cpp")
    file(GLOB_RECURSE WXL_PATCHER_SCRIPT CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/scripts/*/patcher/*.cpp")

    add_executable(wxl-patcher ${WXL_PATCHER_CORE} ${WXL_PATCHER_SCRIPT})
    set_target_properties(wxl-patcher PROPERTIES OUTPUT_NAME "wxl-patcher")
    target_include_directories(wxl-patcher PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(wxl-patcher PRIVATE ${WXL_DEFS})

    # =========================================================================
    # Target 3: d3d9.dll  = the D3D9On12 proxy
    # =========================================================================
    file(GLOB WXL_GPU_SRC CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/*.cpp")

    add_library(d3d9 SHARED ${WXL_GPU_SRC})
    set_target_properties(d3d9 PROPERTIES OUTPUT_NAME "d3d9" PREFIX "")
    target_include_directories(d3d9 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(d3d9 PRIVATE d3d12 dxgi dcomp d3dcompiler)
    target_link_options(d3d9 PRIVATE "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/Proxy.def")
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

    add_executable(WarcraftXLHost
        ${WXL_HOST_CORE}
        ${WXL_HOST_SCRIPT}
        "${CMAKE_CURRENT_SOURCE_DIR}/src/core/Logger.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/deps/flatbuffers/src/util.cpp")
    set_target_properties(WarcraftXLHost PROPERTIES OUTPUT_NAME "WarcraftXLHost")
    target_include_directories(WarcraftXLHost PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/host"
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
