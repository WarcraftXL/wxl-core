# Development checks, never deployed. wxl-seh-check.exe exercises the WXL_SEH_TRY guard of
# wxl/Common.hpp: exit code 0 is a pass.

add_executable(wxl-seh-check "${CMAKE_CURRENT_SOURCE_DIR}/SehCheck.cpp")
target_include_directories(wxl-seh-check PRIVATE "${CMAKE_SOURCE_DIR}/include")
target_compile_definitions(wxl-seh-check PRIVATE ${WXL_DEFS})
