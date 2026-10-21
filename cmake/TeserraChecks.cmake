# Source checks as build targets (docs/guides/build-and-ci.md#layer-and-leak-checks).
#
#   format        format all engine, example, test and tool sources in place
#   format-check  fail if any of them is not formatted
#   check         every check CI runs on sources (currently format-check)
#
# The targets run tools/scripts/*.py and are only defined for a top-level build, when Python 3 and
# clang-format are available. CI runs the same scripts directly, without configuring CMake.

include_guard(GLOBAL)

find_package(Python3 COMPONENTS Interpreter)
find_program(TESERRA_CLANG_FORMAT
    NAMES clang-format
    HINTS "$ENV{VCINSTALLDIR}/Tools/Llvm/x64/bin"
    DOC "clang-format used by the format and format-check targets"
)

if(NOT Python3_Interpreter_FOUND OR NOT TESERRA_CLANG_FORMAT)
    message(STATUS "Teserra: format, format-check and check targets disabled (need Python 3 and clang-format)")
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
add_custom_target(check DEPENDS format-check)
