# WraithEngine.
#
# Two artifacts share this tree: WraithHost.exe (64-bit byte translator + archive/DB owner) and
# Wraith.dll (32-bit engine-runtime shim). They cannot share one toolchain platform, so the file is
# platform-aware: configure -A x64 to build the host, -A Win32 to build the DLL.
#
# The TARGET is always the 335 client. There is no version flag: the source format only adds data
# additively, so a single SOURCE shape is the superset of every modern build and one data-gated
# path translates it. structure/ declares the two shapes per format (Client = 335, Source = modern).

cmake_minimum_required(VERSION 3.21)
project(WraithEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# No version flag: the vendor only adds chunks/fields ADDITIVELY, so one SOURCE shape is the
# additive superset of every modern build and the transforms gate on presence, not version.
# structure/ declares two shapes per format - Client (335 target) and Source (modern) - and one
# data-gated path serves them all.
set(WRAITH_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Shared include roots. core/, ipc/, offsets/ and the contract headers live under src/.
set(WRAITH_INCLUDE_DIRS ${WRAITH_SRC})

# Vendored third-party dependencies, self-contained under deps/.
set(WRAITH_DEPS ${CMAKE_CURRENT_SOURCE_DIR}/deps)

# Optional deployment target. When set, each artifact is copied to the client after build.
set(CLIENT_PATH "" CACHE PATH "Path to the client directory")

# =============================================================================
# Target 1: Wraith.dll  (32-bit, IAT-injected into the client)
#   Live-engine + GPU runtime shim. core/ + ipc/ + offsets/ + runtime/.
#   Requires a Win32 (x86) toolchain. minhook (dep/) is DLL-only.
# =============================================================================
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    add_subdirectory("${WRAITH_DEPS}/minhook" "${CMAKE_BINARY_DIR}/minhook")

    add_library(Wraith SHARED
        ${WRAITH_SRC}/Wraith.cpp
        ${WRAITH_SRC}/core/Hook.cpp
        ${WRAITH_SRC}/core/Logger.cpp
        ${WRAITH_SRC}/core/Mem.cpp
        ${WRAITH_SRC}/runtime/storage/ShmClient.cpp
        ${WRAITH_SRC}/runtime/storage/StorageHook.cpp
        ${WRAITH_SRC}/runtime/wmo/WmoGuards.cpp
        ${WRAITH_SRC}/runtime/wmo/WmoMultiPass.cpp
        ${WRAITH_SRC}/runtime/m2/Loader.cpp
        ${WRAITH_SRC}/runtime/m2/Anim.cpp
        ${WRAITH_SRC}/runtime/m2/Ribbon.cpp
        ${WRAITH_SRC}/runtime/m2/Particle.cpp
        ${WRAITH_SRC}/runtime/m2/Bone.cpp
        ${WRAITH_SRC}/runtime/m2/Mahjong.cpp
        ${WRAITH_SRC}/runtime/adt/Adt.cpp
        ${WRAITH_SRC}/runtime/multipass/MultiPass.cpp
        ${WRAITH_SRC}/runtime/bonessplitter/BonesSplitter.cpp
        ${WRAITH_SRC}/runtime/db2/DB2Mgr.cpp
        ${WRAITH_SRC}/runtime/db2/MapOverride.cpp
        ${WRAITH_SRC}/runtime/db2/DB2FileClient.cpp     # client load path (engine file I/O)
        ${WRAITH_SRC}/host/db2/DB2.cpp                   # decoder (pure, no storage API)
        ${WRAITH_SRC}/host/db2/DB2File.cpp              # table core (LoadBytes path)
        "${WRAITH_DEPS}/flatbuffers/src/util.cpp")
    set_target_properties(Wraith PROPERTIES OUTPUT_NAME "Wraith" PREFIX "")
    target_include_directories(Wraith PRIVATE
        ${WRAITH_SRC}
        ${WRAITH_SRC}/ipc
        ${WRAITH_SRC}/core
        ${WRAITH_SRC}/runtime/storage
        ${WRAITH_SRC}/runtime/wmo
        ${WRAITH_SRC}/runtime/m2
        ${WRAITH_SRC}/runtime/adt
        ${WRAITH_SRC}/runtime/multipass
        ${WRAITH_SRC}/runtime/dynamic_scaling
        ${WRAITH_SRC}/runtime/bonessplitter
        ${WRAITH_SRC}/runtime/db2
        ${WRAITH_SRC}/host/db2
        ${WRAITH_SRC}/structure
        ${WRAITH_SRC}/offsets
        ${WRAITH_SRC}/offsets/engine
        ${WRAITH_SRC}/offsets/game
        "${WRAITH_DEPS}/minhook/include"
        "${WRAITH_DEPS}/flatbuffers/include")
    target_link_libraries(Wraith PRIVATE minhook d3d9)
    target_compile_definitions(Wraith PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS WRAITH_DLL=1)

    # -------------------------------------------------------------------------
    # d3d9.dll: our proxy. Exports the d3d9 API, forwards to the real d3d9, forces D3D9On12 onto our own
    # D3D12 device, and hosts the D3D12-side rendering/capture. Must be 32-bit to load in the client.
    # -------------------------------------------------------------------------
    add_library(d3d9 SHARED
        ${WRAITH_SRC}/runtime/gpu/Device.cpp
        ${WRAITH_SRC}/runtime/gpu/Capture.cpp
        ${WRAITH_SRC}/runtime/gpu/Proxy.cpp)
    set_target_properties(d3d9 PROPERTIES OUTPUT_NAME "d3d9" PREFIX "")
    target_include_directories(d3d9 PRIVATE ${WRAITH_SRC})
    target_link_libraries(d3d9 PRIVATE d3d12)
    target_link_options(d3d9 PRIVATE "/DEF:${WRAITH_SRC}/runtime/gpu/Proxy.def")
    target_compile_definitions(d3d9 PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)

    # -------------------------------------------------------------------------
    # WraithPatcher.exe: IAT injector. Adds Wraith.dll to the target executable's import table.
    # -------------------------------------------------------------------------
    add_executable(WraithPatcher ${WRAITH_SRC}/patcher/Patcher.cpp)
    set_target_properties(WraithPatcher PROPERTIES OUTPUT_NAME "WraithPatcher")
    target_include_directories(WraithPatcher PRIVATE ${WRAITH_SRC})
    target_compile_definitions(WraithPatcher PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)

    # Deploy the 32-bit artifacts to the client after build (set -DCLIENT_PATH=... at configure time).
    if(NOT CLIENT_PATH STREQUAL "")
        add_custom_command(TARGET Wraith POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:Wraith>" "${CLIENT_PATH}/Wraith.dll"
            VERBATIM)
        add_custom_command(TARGET d3d9 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:d3d9>" "${CLIENT_PATH}/d3d9.dll"
            VERBATIM)
        add_custom_command(TARGET WraithPatcher POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:WraithPatcher>" "${CLIENT_PATH}/WraithPatcher.exe"
            VERBATIM)
    endif()
endif()

# =============================================================================
# Target 2: WraithHost.exe  (64-bit)
#   Stateless byte translator + MPQ/DB2 owner. host/ + structure/ + ipc/ + utils/.
#   stormlib + flatbuffers (dep/).
# =============================================================================
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
# StormLib (vendored) supplies MPQ reading for the host. Static, read-only use; no install/tests.
set(STORM_SKIP_INSTALL ON CACHE BOOL "" FORCE)
set(STORM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory("${WRAITH_DEPS}/stormlib" "${CMAKE_BINARY_DIR}/stormlib")

add_executable(WraithHost)
target_sources(WraithHost PRIVATE
    ${WRAITH_SRC}/host/main.cpp
    ${WRAITH_SRC}/host/ipc/ShmServer.cpp
    ${WRAITH_SRC}/host/mpq/MpqStore.cpp
    ${WRAITH_SRC}/host/db2/Db2Store.cpp
    ${WRAITH_SRC}/host/db2/DB2.cpp                    # decoder (pure, no storage API)
    ${WRAITH_SRC}/host/db2/DB2File.cpp               # table core (LoadBytes path)
    ${WRAITH_SRC}/structure/StructureDispatcher.cpp
    ${WRAITH_SRC}/structure/WMO/WmoTranslate.cpp
    ${WRAITH_SRC}/structure/M2/M2Translate.cpp
    ${WRAITH_SRC}/structure/ADT/AdtMerge.cpp
    ${WRAITH_SRC}/structure/WDT/WdtFixup.cpp
    ${WRAITH_SRC}/structure/WDL/WdlRebuild.cpp
    ${WRAITH_SRC}/core/Logger.cpp
    "${WRAITH_DEPS}/flatbuffers/src/util.cpp")       # FlatBuffers runtime (string utils)
target_include_directories(WraithHost PRIVATE
    ${WRAITH_SRC}
    ${WRAITH_SRC}/ipc
    ${WRAITH_SRC}/core
    ${WRAITH_SRC}/host/ipc
    ${WRAITH_SRC}/host/mpq
    ${WRAITH_SRC}/host/db2
    ${WRAITH_SRC}/structure
    "${WRAITH_DEPS}/flatbuffers/include"
    "${WRAITH_DEPS}/stormlib/src")
target_link_libraries(WraithHost PRIVATE storm)
target_compile_definitions(WraithHost PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS WRAITH_HOST=1)

# Deploy the host to the client after build (set -DCLIENT_PATH=... at configure time).
if(NOT CLIENT_PATH STREQUAL "")
    add_custom_command(TARGET WraithHost POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CLIENT_PATH}/Utils"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:WraithHost>" "${CLIENT_PATH}/Utils/WraithHost.exe"
        VERBATIM)
endif()
endif()

# =============================================================================
# Target 3: WraithPatcher.exe  (tool)
#   IAT injector. 32-bit; defined in the EQUAL 4 block alongside the DLL/proxy.
# =============================================================================
