# wxl_deploy(<target> <path>): after each build, copies the target's file to ${CLIENT_PATH}/<path>,
# creating the folder. Without CLIENT_PATH nothing is deployed.

function(wxl_deploy target path)
    if(NOT CLIENT_PATH)
        return()
    endif()
    get_filename_component(wxl_deploy_dir "${CLIENT_PATH}/${path}" DIRECTORY)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${wxl_deploy_dir}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${target}>" "${CLIENT_PATH}/${path}"
        COMMENT "Deploy ${path} -> ${CLIENT_PATH}")
endfunction()
