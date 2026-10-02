# Vendored dependencies as targets: MinHook through its own CMake, Dear ImGui as the objects the core
# links (the core plus the DX9 and Win32 backends; the demo window is deliberately not vendored).

add_subdirectory(minhook)

add_library(wxl_imgui OBJECT
    imgui/imgui.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
    imgui/backends/imgui_impl_dx9.cpp
    imgui/backends/imgui_impl_win32.cpp)
target_include_directories(wxl_imgui PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/imgui"
    "${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends")
target_compile_definitions(wxl_imgui PRIVATE ${WXL_DEFS})
