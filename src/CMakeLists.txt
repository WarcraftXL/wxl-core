# The 32-bit family, injected into the client: WarcraftXL.dll here, wxl-patcher.exe and the d3d9.dll
# proxy in their own folders.

# src/common compiles into every binary (leveled logger, config, shared helpers): the patcher and the
# proxy do not link the engine, so the shared infrastructure is object-level, not a link dependency.
file(GLOB WXL_COMMON_SRC CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/common/*.cpp")
add_library(wxl_common OBJECT ${WXL_COMMON_SRC})
target_include_directories(wxl_common PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(wxl_common PRIVATE ${WXL_DEFS})

# --- WarcraftXL.dll = engine + features + assets pipelines ------------------------------------------
# Automatic, recursive source discovery (no hand-written paths). CONFIGURE_DEPENDS re-globs at build
# time, so dropping a file (or a whole script) needs no edit here. The DLL excludes what other
# targets own: common (wxl_common), the patcher, the probes, the proxy (engine/gpu), and the assets halves that
# belong to exactly one extension (models/m2, models/m3, textures/blp, textures/dds) -- none of them
# has a consumer in the plain DLL, so excluding them here keeps that true instead of shipping four
# unused cross-target .cpp files inside WarcraftXL.dll by glob accident.
file(GLOB_RECURSE WXL_CORE_SRC CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
list(FILTER WXL_CORE_SRC EXCLUDE REGEX
     "/src/common/|/src/patcher/|/src/probe/|/src/engine/gpu/|/src/engine/assets/shared/(models/m2|models/m3|textures/blp|textures/dds)/")

add_library(WarcraftXL SHARED ${WXL_CORE_SRC})
set_target_properties(WarcraftXL PROPERTIES OUTPUT_NAME "WarcraftXL" PREFIX "")
target_include_directories(WarcraftXL PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/include"   # the extension ABI, shared verbatim with extensions
    "${CMAKE_SOURCE_DIR}/deps/minhook/include")
# dwmapi and gdi32: ImGui's Win32 backend names them in #pragma comment(lib), which only MSVC reads.
target_link_libraries(WarcraftXL PRIVATE wxl_common wxl_imgui minhook d3d9 shell32 dwmapi gdi32)
target_compile_definitions(WarcraftXL PRIVATE ${WXL_DEFS})
source_group(TREE "${CMAKE_SOURCE_DIR}" FILES ${WXL_CORE_SRC})

wxl_deploy(WarcraftXL WarcraftXL.dll)

add_subdirectory(patcher)
add_subdirectory(probe)
add_subdirectory(engine/gpu)
