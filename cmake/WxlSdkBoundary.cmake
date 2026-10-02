# The SDK boundary. Core (src/client, src/engine, src/game, src/runtime) legitimately touches
# offsets/ directly -- a detour needs the real address, no amount of indirection removes that. The
# boundary that actually holds is narrower: an EXTENSION should never need offsets/ itself, only
# wxl::game / wxl::events / WXL_Api -- an extension reaching for a raw offset is reaching past the
# SDK surface meant to insulate it from a client-version rebase.

option(WXL_STRICT_SDK_BOUNDARY "Fail the configure when an extension includes offsets/ directly" OFF)

file(GLOB_RECURSE WXL_BOUNDARY_SCAN CONFIGURE_DEPENDS
     "${CMAKE_SOURCE_DIR}/extensions/*.cpp" "${CMAKE_SOURCE_DIR}/extensions/*.hpp")

set(WXL_BOUNDARY_VIOLATIONS "")
foreach(wxl_file IN LISTS WXL_BOUNDARY_SCAN)
    file(STRINGS "${wxl_file}" wxl_hit REGEX "^[ \t]*#include[ \t]+\"(wxl/)?offsets/")
    if(wxl_hit)
        file(RELATIVE_PATH wxl_rel "${CMAKE_SOURCE_DIR}" "${wxl_file}")
        list(APPEND WXL_BOUNDARY_VIOLATIONS "${wxl_rel}")
    endif()
endforeach()

list(LENGTH WXL_BOUNDARY_VIOLATIONS WXL_BOUNDARY_COUNT)
if(WXL_BOUNDARY_COUNT GREATER 0)
    string(REPLACE ";" "\n  " WXL_BOUNDARY_PRETTY "${WXL_BOUNDARY_VIOLATIONS}")
    if(WXL_STRICT_SDK_BOUNDARY)
        message(FATAL_ERROR
            "SDK boundary: ${WXL_BOUNDARY_COUNT} extension file(s) include offsets/ directly:\n  ${WXL_BOUNDARY_PRETTY}")
    endif()
    message(STATUS "SDK boundary: ${WXL_BOUNDARY_COUNT} extension file(s) include offsets/ directly")
endif()
