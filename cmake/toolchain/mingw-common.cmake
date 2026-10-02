# Cross-compiling WarcraftXL from Linux: a clang-based mingw-w64 toolchain producing Windows PE
# binaries (.dll, .exe). Included by mingw-i686.cmake, which sets WXL_MINGW_ARCH.
# Only clang is accepted: the client code uses SEH and MSVC intrinsics that GCC does not compile.
# Lookup order: <arch>-w64-mingw32-clang++ (llvm-mingw, MSYS2 clang) from WXL_MINGW_ROOT, then PATH,
# then ~/.local/opt/llvm-mingw; last, the host clang++ with --target and the distribution's mingw
# sysroot (Fedora: /usr/<triple>/sys-root/mingw).

if(NOT WXL_MINGW_ARCH)
    message(FATAL_ERROR "WXL_MINGW_ARCH must be set (include mingw-i686.cmake)")
endif()

set(CMAKE_SYSTEM_NAME Windows)
if(WXL_MINGW_ARCH STREQUAL "i686")
    set(CMAKE_SYSTEM_PROCESSOR x86)
else()
    set(CMAKE_SYSTEM_PROCESSOR AMD64)
endif()

set(WXL_MINGW_TRIPLE "${WXL_MINGW_ARCH}-w64-mingw32")
set(WXL_MINGW_ROOT "$ENV{WXL_MINGW_ROOT}" CACHE PATH "Root of an llvm-mingw install (its bin/ holds <triple>-clang++)")

set(_wxl_hints "")
if(WXL_MINGW_ROOT)
    list(APPEND _wxl_hints "${WXL_MINGW_ROOT}/bin")
endif()
list(APPEND _wxl_hints "$ENV{HOME}/.local/opt/llvm-mingw/bin")

find_program(WXL_MINGW_CXX NAMES "${WXL_MINGW_TRIPLE}-clang++" HINTS ${_wxl_hints})
find_program(WXL_MINGW_CC  NAMES "${WXL_MINGW_TRIPLE}-clang"   HINTS ${_wxl_hints})

if(WXL_MINGW_CXX AND WXL_MINGW_CC)
    set(CMAKE_C_COMPILER   "${WXL_MINGW_CC}")
    set(CMAKE_CXX_COMPILER "${WXL_MINGW_CXX}")
    get_filename_component(_wxl_bin "${WXL_MINGW_CXX}" DIRECTORY)
    find_program(CMAKE_RC_COMPILER NAMES "${WXL_MINGW_TRIPLE}-windres" llvm-windres HINTS "${_wxl_bin}")
else()
    # Host clang, retargeted. The sysroot is the distribution's mingw-w64 headers + CRT; the GCC-flavoured
    # libstdc++ shipped with it is what clang links against.
    find_program(_wxl_host_clangxx NAMES clang++ REQUIRED)
    find_program(_wxl_host_clang   NAMES clang   REQUIRED)
    set(CMAKE_C_COMPILER   "${_wxl_host_clang}")
    set(CMAKE_CXX_COMPILER "${_wxl_host_clangxx}")
    set(CMAKE_C_COMPILER_TARGET   "${WXL_MINGW_TRIPLE}")
    set(CMAKE_CXX_COMPILER_TARGET "${WXL_MINGW_TRIPLE}")
    if(EXISTS "/usr/${WXL_MINGW_TRIPLE}/sys-root/mingw")
        set(CMAKE_SYSROOT "/usr/${WXL_MINGW_TRIPLE}/sys-root/mingw")
    endif()
    find_program(CMAKE_RC_COMPILER NAMES "${WXL_MINGW_TRIPLE}-windres" llvm-windres)
    find_program(_wxl_lld NAMES ld.lld lld)
    if(_wxl_lld)
        set(CMAKE_EXE_LINKER_FLAGS_INIT    "-fuse-ld=lld")
        set(CMAKE_SHARED_LINKER_FLAGS_INIT "-fuse-ld=lld")
    endif()
endif()

# Nothing from the Linux host is a valid Windows dependency.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
