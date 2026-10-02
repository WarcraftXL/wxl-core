# d3d9.dll: the native proxy (system-d3d9 passthrough + runtime loader). Proxy.def is listed as a
# source: every generator hands it to the linker as the export list.

file(GLOB WXL_GPU_SRC CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_library(d3d9 SHARED ${WXL_GPU_SRC} "${CMAKE_CURRENT_SOURCE_DIR}/Proxy.def")
set_target_properties(d3d9 PROPERTIES OUTPUT_NAME "d3d9" PREFIX "")
# The proxy shadows a static import of Wow.exe, so a hard dependency of the proxy is a hard
# dependency of the game. It resolves the system d3d9 at runtime and links nothing beyond the CRT.
target_link_libraries(d3d9 PRIVATE wxl_common)
target_compile_definitions(d3d9 PRIVATE ${WXL_DEFS})

wxl_deploy(d3d9 d3d9.dll)
