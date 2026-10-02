# Development checks, never deployed; exit code 0 is a pass. wxl-seh-check.exe exercises the
# WXL_SEH_TRY guard of wxl/Common.hpp, wxl-script-check.exe the script layer of wxl/Script.hpp.

add_executable(wxl-seh-check "${CMAKE_CURRENT_SOURCE_DIR}/SehCheck.cpp")
target_include_directories(wxl-seh-check PRIVATE "${CMAKE_SOURCE_DIR}/include")
target_compile_definitions(wxl-seh-check PRIVATE ${WXL_DEFS})

add_executable(wxl-script-check "${CMAKE_CURRENT_SOURCE_DIR}/ScriptCheck.cpp")
target_include_directories(wxl-script-check PRIVATE "${CMAKE_SOURCE_DIR}/include")
target_compile_definitions(wxl-script-check PRIVATE ${WXL_DEFS})
