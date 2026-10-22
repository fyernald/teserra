# Source checks as build targets (docs/guides/build-and-ci.md#layer-and-leak-checks).
#
#   layer-check   fail if a module includes headers of a module it does not depend on
#   leak-check    fail if GL/Vulkan/GLFW/Win32/glm names leave their place, or a TODO names no issue
#   format        format all engine, example, test and tool sources in place
#   format-check  fail if any of them is not formatted
#   check         every check CI runs on sources
#
# The targets run tools/scripts/*.py and are only defined for a top-level build. They need Python 3;
# format and format-check also need clang-format. CI runs the same scripts directly, without configuring
# CMake.

include_guard(GLOBAL)

find_package(Python3 COMPONENTS Interpreter)
if(NOT Python3_Interpreter_FOUND)
    message(STATUS "Teserra: check targets disabled (need Python 3)")
    return()
endif()

add_custom_target(layer-check
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/scripts/check_layers.py"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    COMMENT "Checking module includes against DEPENDS"
    USES_TERMINAL
    VERBATIM
)
add_custom_target(leak-check
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/scripts/check_gl_leaks.py"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    COMMENT "Checking for backend, platform and glm leaks"
    USES_TERMINAL
    VERBATIM
)
add_custom_target(check DEPENDS layer-check leak-check)

find_program(TESERRA_CLANG_FORMAT
    NAMES clang-format
    HINTS "$ENV{VCINSTALLDIR}/Tools/Llvm/x64/bin"
    DOC "clang-format used by the format and format-check targets"
)
if(NOT TESERRA_CLANG_FORMAT)
    message(STATUS "Teserra: format and format-check targets disabled (need clang-format)")
    return()
endif()

set(_teserra_check_format
    "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/scripts/check_format.py"
    --clang-format "${TESERRA_CLANG_FORMAT}"
)

add_custom_target(format
    COMMAND ${_teserra_check_format} --fix
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    COMMENT "Formatting sources with clang-format"
    USES_TERMINAL
    VERBATIM
)
add_custom_target(format-check
    COMMAND ${_teserra_check_format}
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    COMMENT "Checking source formatting"
    USES_TERMINAL
    VERBATIM
)
add_dependencies(check format-check)
