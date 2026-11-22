# Build-system test of the path length check of teserra_add_shaders() (cmake/TeserraShaders.cmake, #192),
# registered by tests/cmake/CMakeLists.txt as cmake.shaders.path_length on Windows. A generated path of 259
# characters passes; one of 260 fails configuration with a message that names the path, its length and the
# fix, which the test's PASS_REGULAR_EXPRESSION matches. A real build directory that deep cannot be used:
# CMake's own files of the compiler checks would exceed the limit first.
#
#   cmake -DTESERRA_SOURCE_DIR=<repo> -P TestShaderPathLength.cmake

include("${TESERRA_SOURCE_DIR}/cmake/TeserraShaders.cmake")

function(_path_of_length out length)
    set(prefix "C:/build/")
    string(LENGTH "${prefix}" prefix_length)
    math(EXPR padding "${length} - ${prefix_length} - 8") # 8: "a.vert.d"
    string(REPEAT "d" ${padding} directory)
    set(${out} "${prefix}${directory}a.vert.d" PARENT_SCOPE)
endfunction()

_path_of_length(fits 259)
_teserra_check_generated_path("teserra_add_shaders(fits)" "${fits}")
message(STATUS "TestShaderPathLength: 259 characters pass")

_path_of_length(too_long 260)
_teserra_check_generated_path("teserra_add_shaders(deep)" "${too_long}")
message(FATAL_ERROR "TestShaderPathLength: a path of 260 characters passed the check")
