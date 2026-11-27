# Tests of the build system itself. Each case configures a fixture project in its own build directory
# with the same generator and compiler as this build and checks the outcome. The fixtures find
# dependencies where this build found them (the vcpkg installed tree); the list separators are escaped
# so the path list stays one argument.
string(REPLACE ";" "$<SEMICOLON>" _teserra_fixture_prefix_path "${CMAKE_PREFIX_PATH}")
set(_teserra_fixture_args
    -G "${CMAKE_GENERATOR}"
    "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
    "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
    "-DCMAKE_PREFIX_PATH=${_teserra_fixture_prefix_path}"
    # The fixtures find their packages in this build's vcpkg tree, which has ImGui only when this build does.
    "-DTESERRA_WITH_DEBUG_UI=${TESERRA_WITH_DEBUG_UI}"
)

function(_teserra_add_layer_rule_test case)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "EXPECT_OUTPUT" "")
    set(name "cmake.layer_rule.${case}")
    add_test(NAME ${name}
        COMMAND "${CMAKE_COMMAND}"
            -S "${CMAKE_CURRENT_SOURCE_DIR}/layer_rule"
            -B "${CMAKE_CURRENT_BINARY_DIR}/layer_rule_${case}"
            ${_teserra_fixture_args}
            "-DTESERRA_FIXTURE_CASE=${case}"
    )
    if(arg_EXPECT_OUTPUT)
        # The expected message only appears when configuration failed, so matching it is enough.
        # CMake wraps long messages, so any space in the expectation also matches a line break.
        string(REPLACE " " "[ \t\r\n]+" regex "${arg_EXPECT_OUTPUT}")
        set_tests_properties(${name} PROPERTIES PASS_REGULAR_EXPRESSION "${regex}")
    endif()
    set_tests_properties(${name} PROPERTIES LABELS "cmake")
endfunction()

_teserra_add_layer_rule_test(accepts_lower_layers)
_teserra_add_layer_rule_test(rejects_higher_layer
    EXPECT_OUTPUT "Layer violation: module 'core' \\(layer 0\\) must not depend on module 'platform' \\(layer 1\\)"
)
_teserra_add_layer_rule_test(rejects_same_layer
    EXPECT_OUTPUT "Layer violation: module 'assets' \\(layer 2\\) must not depend on module 'rhi' \\(layer 2\\)"
)
_teserra_add_layer_rule_test(rejects_unknown_module
    EXPECT_OUTPUT "unknown module 'platform' in DEPENDS"
)
_teserra_add_layer_rule_test(optional_dependencies)
_teserra_add_layer_rule_test(rejects_higher_optional_layer
    EXPECT_OUTPUT "Layer violation: module 'platform' \\(layer 1\\) must not depend on module 'rhi' \\(layer 2\\)"
)

# Teserra consumed through add_subdirectory() keeps tests and examples off by default. A clean build
# directory every time, so options cached by an earlier run cannot hide a changed default.
add_test(NAME cmake.subproject.tests_and_examples_default_off
    COMMAND "${CMAKE_COMMAND}"
        -S "${CMAKE_CURRENT_SOURCE_DIR}/subproject"
        -B "${CMAKE_CURRENT_BINARY_DIR}/subproject"
        --fresh
        ${_teserra_fixture_args}
)
set_tests_properties(cmake.subproject.tests_and_examples_default_off PROPERTIES LABELS "cmake")

# The options that add flags (warnings as errors, sanitizers) change only Teserra's own targets, not the
# game's (#120). The fixture compares the parent's flags and directory properties before and after.
add_test(NAME cmake.subproject.options_do_not_leak
    COMMAND "${CMAKE_COMMAND}"
        -S "${CMAKE_CURRENT_SOURCE_DIR}/subproject"
        -B "${CMAKE_CURRENT_BINARY_DIR}/subproject_options"
        --fresh
        ${_teserra_fixture_args}
        -DTESERRA_WARNINGS_AS_ERRORS=ON
        -DTESERRA_ENABLE_SANITIZERS=ON
)
set_tests_properties(cmake.subproject.options_do_not_leak PROPERTIES LABELS "cmake")

# The installed package (#143): cmake --install into a scratch prefix, then a game built against it with
# find_package(Teserra <major>.<minor>); another minor version is refused.
if(TESERRA_INSTALL AND TARGET teserra_engine)
    math(EXPR _teserra_other_minor "${PROJECT_VERSION_MINOR} + 1")
    add_test(NAME cmake.install.find_package
        COMMAND "${CMAKE_COMMAND}"
            "-DTESERRA_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
            "-DTESERRA_BUILD_DIR=${PROJECT_BINARY_DIR}"
            "-DTESERRA_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/install"
            "-DTESERRA_VERSION=${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
            "-DTESERRA_OTHER_VERSION=${PROJECT_VERSION_MAJOR}.${_teserra_other_minor}"
            "-DTESERRA_GENERATOR=${CMAKE_GENERATOR}"
            "-DTESERRA_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
            "-DTESERRA_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
            "-DTESERRA_PREFIX_PATH=${_teserra_fixture_prefix_path}"
            "-DTESERRA_BUILD_TYPE=$<CONFIG>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/install/TestInstall.cmake"
    )
    set_tests_properties(cmake.install.find_package PROPERTIES LABELS "cmake" TIMEOUT 600)
endif()

# templates/minimal_game lists the libraries Teserra builds with and uses its vcpkg baseline (#120).
add_test(NAME cmake.template.manifest_matches
    COMMAND "${CMAKE_COMMAND}" "-DTESERRA_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
        -P "${PROJECT_SOURCE_DIR}/cmake/scripts/CheckTemplateManifest.cmake"
)
set_tests_properties(cmake.template.manifest_matches PROPERTIES LABELS "cmake")

# glm settings come only from teserra_core's PUBLIC compile definitions (ADR-0008).
add_test(NAME cmake.glm_configured_only_by_core
    COMMAND "${CMAKE_COMMAND}" "-DTESERRA_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
        -P "${PROJECT_SOURCE_DIR}/cmake/scripts/CheckGlmConfig.cmake"
)
set_tests_properties(cmake.glm_configured_only_by_core PROPERTIES LABELS "cmake")

# teserra_add_shaders(): full, no-op and incremental builds, shader errors and name clashes in a scratch copy of
# the fixture in shaders/project/.
add_test(NAME cmake.shaders.incremental
    COMMAND "${CMAKE_COMMAND}"
        "-DTESERRA_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "-DTESERRA_SHADERC=$<TARGET_FILE:teserra_shaderc>"
        "-DTESERRA_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/shaders"
        "-DTESERRA_GENERATOR=${CMAKE_GENERATOR}"
        "-DTESERRA_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
        "-DTESERRA_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/shaders/TestShaderBuild.cmake"
)
set_tests_properties(cmake.shaders.incremental PROPERTIES LABELS "cmake;shaderc")

# Generated shader paths that Windows cannot write fail configuration with a clear message (#192).
if(CMAKE_HOST_WIN32)
    add_test(NAME cmake.shaders.path_length
        COMMAND "${CMAKE_COMMAND}" "-DTESERRA_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/shaders/TestShaderPathLength.cmake"
    )
    # CMake wraps long messages, so spaces in the expectation also match line breaks.
    string(REPLACE " " "[ \t\r\n]+" _teserra_path_length_regex
        "teserra_add_shaders\\(deep\\): the generated file C:/build/d+a[.]vert[.]d would have 260 characters, more than the 259 that Windows allows")
    set_tests_properties(cmake.shaders.path_length PROPERTIES
        PASS_REGULAR_EXPRESSION "${_teserra_path_length_regex}"
        LABELS "cmake;shaderc"
    )
endif()

# Profiling off builds contain no Tracy; profile builds do (#107). teserra_core_tests uses every
# TESERRA_PROFILE_* macro.
add_test(NAME cmake.profiling.tracy_only_when_enabled
    COMMAND "${CMAKE_COMMAND}"
        "-DFILE=$<TARGET_FILE:teserra_core_tests>"
        "-DEXPECT_TRACY=$<BOOL:${TESERRA_ENABLE_PROFILING}>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/check_tracy.cmake"
)
set_tests_properties(cmake.profiling.tracy_only_when_enabled PROPERTIES LABELS "cmake")
