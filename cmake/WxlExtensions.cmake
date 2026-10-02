# wxl_add_extensions(<dir>): one DLL per folder under <dir>, deployed to Extensions/<Name>/<Name>.dll
# and discovered by the core at runtime. Extensions link nothing of the core: the SDK is header-only
# and compiles in, and the rest arrives through the API table.

function(wxl_add_extensions root)
    # The SDK's out-of-line half. Everything else under src/game is inline, but the gx facade creates
    # D3D resources and tracks them for the device reset, so each binary compiles and owns its own --
    # an extension releases its targets from OnDeviceLost rather than through the core's sweep.
    file(GLOB WXL_SDK_SRC CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/game/*.cpp")

    file(GLOB wxl_ext_dirs "${root}/*")
    foreach(wxl_ext_dir IN LISTS wxl_ext_dirs)
        if(NOT IS_DIRECTORY "${wxl_ext_dir}")
            continue()
        endif()
        get_filename_component(wxl_ext_name "${wxl_ext_dir}" NAME)
        file(GLOB_RECURSE WXL_EXT_SRC CONFIGURE_DEPENDS "${wxl_ext_dir}/*.cpp")
        if(NOT WXL_EXT_SRC)
            continue()
        endif()

        # An extension that needs cross-target-shared source (src/engine/assets/shared/...) declares it
        # in its own extensions/<name>/shared.cmake (populates WXL_EXT_SHARED_SRC) instead of this loop
        # hardcoding extension names -- drop a new extension in, give it a shared.cmake if it needs
        # one, and it's picked up with no edit here. WXL_EXT_INCLUDE_DIRS is the same arrangement for
        # headers an extension vendors itself, so a dependency only it uses stays in its own repository
        # rather than in the core's deps/. Reset before each include: file(GLOB) in the included script
        # would otherwise accumulate the previous iteration's stale value.
        set(WXL_EXT_SHARED_SRC "")
        set(WXL_EXT_INCLUDE_DIRS "")
        include("${wxl_ext_dir}/shared.cmake" OPTIONAL)
        list(APPEND WXL_EXT_SRC ${WXL_EXT_SHARED_SRC})

        add_library(${wxl_ext_name} SHARED ${WXL_EXT_SRC} ${WXL_SDK_SRC})
        set_target_properties(${wxl_ext_name} PROPERTIES OUTPUT_NAME "${wxl_ext_name}" PREFIX "")
        target_include_directories(${wxl_ext_name} PRIVATE
            "${CMAKE_SOURCE_DIR}/include"
            "${CMAKE_SOURCE_DIR}/src"
            ${WXL_EXT_INCLUDE_DIRS})
        # WXL_EXTENSION is what makes PluginApi.h declare the two entry points as exports. It belongs on
        # the target rather than in a source file: an extension that forgets it still builds, and fails
        # only at load with "exports no WXL_Query/WXL_Load".
        target_compile_definitions(${wxl_ext_name} PRIVATE ${WXL_DEFS} WXL_EXTENSION)

        wxl_deploy(${wxl_ext_name} "Extensions/${wxl_ext_name}/${wxl_ext_name}.dll")
    endforeach()
endfunction()
