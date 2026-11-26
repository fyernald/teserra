# Copying a game's asset directory next to its executable (docs/guides/build-and-ci.md#using-teserra-in-your-game).
#
#   teserra_copy_assets(<target> [SOURCE_DIR <dir>] [DESTINATION <dir>])
#
# Copies the files under SOURCE_DIR (default: assets, relative to the current source directory) into
# DESTINATION (default: assets) in <target>'s output directory, where engine::App mounts assets:// by default.
# The copy runs on every build of <target>, before it, and only writes files that changed, so editing an
# asset needs no reconfigure. Files deleted from SOURCE_DIR stay in the output until it is cleaned.
#
# Included by the root CMakeLists.txt, so a game that adds Teserra with add_subdirectory() or FetchContent can
# call it.

include_guard(GLOBAL)

function(teserra_copy_assets target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "SOURCE_DIR;DESTINATION" "")
    if(arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "teserra_copy_assets(${target}): unknown arguments: ${arg_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT TARGET ${target})
        message(FATAL_ERROR "teserra_copy_assets: '${target}' is not a target")
    endif()
    if(NOT DEFINED arg_SOURCE_DIR)
        set(arg_SOURCE_DIR assets)
    endif()
    if(NOT DEFINED arg_DESTINATION)
        set(arg_DESTINATION assets)
    endif()

    cmake_path(ABSOLUTE_PATH arg_SOURCE_DIR BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE
               OUTPUT_VARIABLE source)
    if(NOT IS_DIRECTORY "${source}")
        message(FATAL_ERROR "teserra_copy_assets(${target}): '${source}' is not a directory")
    endif()
    if(IS_ABSOLUTE "${arg_DESTINATION}" OR arg_DESTINATION MATCHES "(^|/)\\.\\.(/|$)")
        message(FATAL_ERROR
            "teserra_copy_assets(${target}): DESTINATION '${arg_DESTINATION}' must be a relative path inside the "
            "target's output directory")
    endif()

    set(destination "$<TARGET_FILE_DIR:${target}>/${arg_DESTINATION}")
    add_custom_target(${target}_assets
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${destination}"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory_if_different "${source}" "${destination}"
        COMMENT "Copying the assets of ${target}"
        VERBATIM
    )
    add_dependencies(${target} ${target}_assets)
endfunction()
