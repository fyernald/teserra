# Build-system test of teserra_add_shaders() (cmake/TeserraShaders.cmake), registered by
# tests/cmake/CMakeLists.txt as cmake.shaders.incremental. It copies the fixture project in project/ to a
# scratch directory, builds it with Ninja and edits its shaders:
#
# 1. A full build compiles and embeds the shaders, and the fixture's executable accepts the embedded data.
# 2. A second build has no work to do.
# 3. Touching a file that one shader includes recompiles only that shader; touching another shader
#    recompiles only that one.
# 4. An error in a shader fails the build with the tool's file(line): error: message.
# 5. Two embedded shaders with the same name fail configuration.
#
#   cmake -DTESERRA_SOURCE_DIR=<repo> -DTESERRA_SHADERC=<exe> -DTESERRA_WORK_DIR=<dir>
#         -DTESERRA_GENERATOR=<generator> -DTESERRA_MAKE_PROGRAM=<ninja> -DTESERRA_CXX_COMPILER=<cxx>
#         -P TestShaderBuild.cmake

if(NOT TESERRA_GENERATOR MATCHES "Ninja")
    # The checks read Ninja's output; other generators get the same rules from CMake.
    message("TestShaderBuild: skipped, needs a Ninja generator (this build uses ${TESERRA_GENERATOR})")
    return()
endif()

set(source "${TESERRA_WORK_DIR}/project")
set(build "${TESERRA_WORK_DIR}/build")
file(REMOVE_RECURSE "${TESERRA_WORK_DIR}")
file(COPY "${CMAKE_CURRENT_LIST_DIR}/project" DESTINATION "${TESERRA_WORK_DIR}")

function(_run description)
    cmake_parse_arguments(PARSE_ARGV 1 arg "FAILS" "OUTPUT" "COMMAND")
    execute_process(COMMAND ${arg_COMMAND} RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
    if(arg_FAILS AND result EQUAL 0)
        message(FATAL_ERROR "TestShaderBuild: ${description} succeeded, but should have failed:\n${output}")
    elseif(NOT arg_FAILS AND NOT result EQUAL 0)
        message(FATAL_ERROR "TestShaderBuild: ${description} failed (${result}):\n${output}")
    endif()
    message(STATUS "TestShaderBuild: ${description}: ok")
    set(${arg_OUTPUT} "${output}" PARENT_SCOPE)
endfunction()

function(_expect output regex description)
    if(NOT output MATCHES "${regex}")
        message(FATAL_ERROR "TestShaderBuild: ${description}: the output does not match '${regex}':\n${output}")
    endif()
endfunction()

function(_expect_not output regex description)
    if(output MATCHES "${regex}")
        message(FATAL_ERROR "TestShaderBuild: ${description}: the output matches '${regex}':\n${output}")
    endif()
endfunction()

set(configure "${CMAKE_COMMAND}" -S "${source}" -G "${TESERRA_GENERATOR}"
    "-DCMAKE_MAKE_PROGRAM=${TESERRA_MAKE_PROGRAM}"
    "-DCMAKE_CXX_COMPILER=${TESERRA_CXX_COMPILER}"
    "-DTESERRA_SOURCE_DIR=${TESERRA_SOURCE_DIR}"
    "-DTESERRA_SHADERC=${TESERRA_SHADERC}"
)
set(rebuild "${CMAKE_COMMAND}" --build "${build}")

# 1. Full build.
_run("configure" COMMAND ${configure} -B "${build}" OUTPUT output)
_run("full build" COMMAND ${rebuild} OUTPUT output)
_expect("${output}" "Compiling shader shaders/a\\.vert" "full build")
_expect("${output}" "Embedding shader shaders/b\\.frag" "full build")
_run("embedded data" COMMAND "${build}/embedded" OUTPUT output)
_expect("${output}" "embedded shaders ok" "embedded data")
if(NOT EXISTS "${build}/assets/shaders/a.vert.tshader")
    message(FATAL_ERROR "TestShaderBuild: the shader file build/assets/shaders/a.vert.tshader is missing")
endif()

# 2. Nothing changed, nothing runs: no shader is recompiled on every build.
_run("no-op build" COMMAND ${rebuild} OUTPUT output)
_expect("${output}" "no work to do" "no-op build")

# 3. Only the shaders that see a change are recompiled. The pause keeps the new times apart from the build's
# on file systems with coarse timestamps.
execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 1)
file(TOUCH "${source}/shaders/include/common.glsl")
_run("build after touching an include" COMMAND ${rebuild} OUTPUT output)
_expect("${output}" "Compiling shader shaders/a\\.vert" "build after touching an include")
_expect_not("${output}" "Compiling shader shaders/b\\.frag" "build after touching an include")

execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 1)
file(TOUCH "${source}/shaders/b.frag")
_run("build after touching a shader" COMMAND ${rebuild} OUTPUT output)
_expect("${output}" "Compiling shader shaders/b\\.frag" "build after touching a shader")
_expect_not("${output}" "Compiling shader shaders/a\\.vert" "build after touching a shader")

# 4. A shader error fails the build with the tool's message (MSVC style on Windows, GCC style elsewhere).
file(WRITE "${source}/shaders/b.frag" "#version 450\nlayout(location = 0) out vec4 fragColor;\nvoid main() { fragColor = undeclaredTint; }\n")
_run("build with a shader error" FAILS COMMAND ${rebuild} OUTPUT output)
_expect("${output}" "b\\.frag(\\(3\\)|:3): error: 'undeclaredTint' : undeclared identifier"
    "build with a shader error")

# 5. Two embedded shaders named kBFrag.
_run("configure with duplicate names" FAILS
    COMMAND ${configure} -B "${TESERRA_WORK_DIR}/duplicates" -DTESERRA_FIXTURE_CASE=duplicate_names OUTPUT output)
_expect("${output}" "two shaders are both named kBFrag" "configure with duplicate names")
