# wxl-patcher.exe: the offline PE patcher (incl. the ex-wxl-no-gluexml example, caught by the glob).

file(GLOB_RECURSE WXL_PATCHER_SRC CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_executable(wxl-patcher ${WXL_PATCHER_SRC})
set_target_properties(wxl-patcher PROPERTIES OUTPUT_NAME "wxl-patcher")
target_link_libraries(wxl-patcher PRIVATE wxl_common)
target_compile_definitions(wxl-patcher PRIVATE ${WXL_DEFS})
