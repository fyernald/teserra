# Test helpers (docs/guides/testing-strategy.md, ADR-0012).
#
#   teserra_add_test(<name> [NO_MAIN] SOURCES <file>... [DEPENDS <module>...] [LIBRARIES <target>...])
#
# Creates the Catch2 test executable teserra_<name>_tests. Module unit tests use the module name
# (teserra_add_test(core ...) -> teserra_core_tests). DEPENDS links Teserra modules, LIBRARIES any
# other target. Every TEST_CASE is registered as its own CTest test, labeled <name>, so `ctest -N`,
# IDE test explorers and the JUnit report list individual test cases. Every test executable also links
# teserra_test_support (tests/support/): the <teserra/test/...> helpers, a listener that turns off
# console logging and makes failed engine assertions fail the current test instead of aborting, and
# global operator new/delete that count allocations per thread (<teserra/test/allocation_counter.hpp>).
#
#   teserra_add_header_check(<module>)
#
# Compiles every public header of <module> in a translation unit of its own, the way a user of the
# module sees it: with the module's public include directories and dependencies and the Teserra warning
# set. The build fails if a header is not self-contained or exposes a private dependency: spdlog or fmt
# (private to teserra_core, ADR-0011), GLFW (private to teserra_platform, ADR-0006), <windows.h>
# (ADR-0015), OpenGL headers (private to teserra_rhi_opengl, ADR-0009) or stb_image (private to
# teserra_assets, ADR-0010).
#
#   teserra_add_compile_fail_test(<name> SOURCE <file> EXPECT <regex> [WARNINGS_AS_ERRORS]
#       [DEPENDS <module>...])
#
# Registers the CTest test <name>, which builds <file> (excluded from the normal build) and passes only
# if the compiler output matches <regex>. For guarantees that are compile errors, such as checked format
# strings. Make <regex> specific to the expected error, so that an unrelated failure does not pass.
# WARNINGS_AS_ERRORS compiles <file> with /WX or -Werror whatever TESERRA_WARNINGS_AS_ERRORS says, for
# guarantees that are warnings (otherwise the object would build and later runs would see no output).
#
#   teserra_add_abort_test(<name> SOURCE <file> EXPECT <regex> [DEPENDS <module>...])
#
# Registers the CTest test <name>, which runs a program built from <file> and passes only if it ends
# abnormally (non-zero exit) and its output matches <regex>. For behavior such as TESERRA_FATAL that
# ends the process and so cannot be tested inside Catch2.

include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/TeserraWarnings.cmake")

find_package(Catch2 3 CONFIG REQUIRED)
include(Catch)

function(teserra_add_test name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "NO_MAIN" "" "SOURCES;DEPENDS;LIBRARIES")
    if(arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "teserra_add_test(${name}): unknown arguments: ${arg_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT arg_SOURCES)
        message(FATAL_ERROR "teserra_add_test(${name}): SOURCES is required")
    endif()

    set(target teserra_${name}_tests)
    add_executable(${target} ${arg_SOURCES})
    _teserra_configure_test_target(${target})

    foreach(module IN LISTS arg_DEPENDS)
        if(NOT TARGET teserra_${module})
            message(FATAL_ERROR "teserra_add_test(${name}): unknown module '${module}' in DEPENDS")
        endif()
        target_link_libraries(${target} PRIVATE teserra_${module})
    endforeach()
    # NO_MAIN: the sources have their own main(), for options of their own (the snapshot tests'
    # --update-goldens).
    if(arg_NO_MAIN)
        target_link_libraries(${target} PRIVATE teserra_test_support Catch2::Catch2 ${arg_LIBRARIES})
    else()
        target_link_libraries(${target} PRIVATE teserra_test_support Catch2::Catch2WithMain ${arg_LIBRARIES})
    endif()

    # PRE_TEST discovers test cases when CTest runs instead of after the build, so the build does not
    # execute test binaries and runtime DLLs deployed after linking are in place.
    catch_discover_tests(${target}
        DISCOVERY_MODE PRE_TEST
        PROPERTIES LABELS ${name}
    )
endfunction()

# Settings shared by test-only targets: the language level and warning set of engine modules.
function(_teserra_configure_test_target target)
    set_target_properties(${target} PROPERTIES
        CXX_EXTENSIONS OFF
        CXX_SCAN_FOR_MODULES OFF
    )
    target_compile_features(${target} PRIVATE cxx_std_20)
    teserra_set_warnings(${target})
endfunction()

function(teserra_add_header_check module)
    set(module_target teserra_${module})
    if(NOT TARGET ${module_target})
        message(FATAL_ERROR "teserra_add_header_check(${module}): '${module}' is not a Teserra module")
    endif()

    get_target_property(headers ${module_target} HEADER_SET)
    get_target_property(base_dirs ${module_target} HEADER_DIRS)
    get_target_property(module_source_dir ${module_target} SOURCE_DIR)
    if(NOT headers)
        message(FATAL_ERROR "teserra_add_header_check(${module}): the module has no public headers")
    endif()

    set(check_target teserra_${module}_header_check)
    set(sources "")
    foreach(header IN LISTS headers)
        cmake_path(ABSOLUTE_PATH header BASE_DIRECTORY "${module_source_dir}" NORMALIZE)
        set(include_path "")
        foreach(base_dir IN LISTS base_dirs)
            cmake_path(IS_PREFIX base_dir "${header}" NORMALIZE inside_base_dir)
            if(inside_base_dir)
                file(RELATIVE_PATH include_path "${base_dir}" "${header}")
                break()
            endif()
        endforeach()
        if(NOT include_path)
            message(FATAL_ERROR "teserra_add_header_check(${module}): ${header} is in no header base directory")
        endif()

        string(MAKE_C_IDENTIFIER "${include_path}" source_name)
        set(source "${CMAKE_CURRENT_BINARY_DIR}/${check_target}/${source_name}.cpp")
        file(CONFIGURE OUTPUT "${source}" @ONLY CONTENT [[
// Generated by teserra_add_header_check(@module@): <@include_path@> must compile on its own.
#include <@include_path@>

#if defined(SPDLOG_VERSION) || defined(FMT_VERSION)
#error "<@include_path@> exposes spdlog or fmt, which are private to teserra_core (ADR-0011)"
#endif
#if defined(GLFW_VERSION_MAJOR)
#error "<@include_path@> exposes GLFW, which is private to teserra_platform (ADR-0006)"
#endif
#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error "<@include_path@> includes <windows.h>, which belongs in engine/platform/src/ only (ADR-0015)"
#endif
#if defined(GLAD_GL_H_) || defined(__gl_h_) || defined(__GL_H__)
#error "<@include_path@> exposes OpenGL headers, which are private to teserra_rhi_opengl (ADR-0009)"
#endif
#if defined(STBI_VERSION)
#error "<@include_path@> exposes stb_image, which is private to teserra_assets (ADR-0010)"
#endif
#if defined(YYJSON_VERSION_HEX)
#error "<@include_path@> exposes yyjson, which is private to teserra_assets (ADR-0025)"
#endif
#if defined(MA_VERSION_MAJOR)
#error "<@include_path@> exposes miniaudio, which is private to teserra_audio (ADR-0027)"
#endif
]])
        list(APPEND sources "${source}")
    endforeach()

    add_library(${check_target} OBJECT ${sources})
    _teserra_configure_test_target(${check_target})
    target_link_libraries(${check_target} PRIVATE ${module_target})
endfunction()

function(teserra_add_compile_fail_test name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "WARNINGS_AS_ERRORS" "SOURCE;EXPECT" "DEPENDS")
    if(arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "teserra_add_compile_fail_test(${name}): unknown arguments: ${arg_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT arg_SOURCE OR NOT arg_EXPECT)
        message(FATAL_ERROR "teserra_add_compile_fail_test(${name}): SOURCE and EXPECT are required")
    endif()

    string(MAKE_C_IDENTIFIER "teserra_compile_fail_${name}" target)
    add_library(${target} OBJECT EXCLUDE_FROM_ALL ${arg_SOURCE})
    _teserra_configure_test_target(${target})
    if(arg_WARNINGS_AS_ERRORS)
        target_compile_options(${target} PRIVATE $<IF:$<BOOL:${MSVC}>,/WX,-Werror>)
    endif()
    foreach(module IN LISTS arg_DEPENDS)
        if(NOT TARGET teserra_${module})
            message(FATAL_ERROR "teserra_add_compile_fail_test(${name}): unknown module '${module}' in DEPENDS")
        endif()
        target_link_libraries(${target} PRIVATE teserra_${module})
    endforeach()

    add_test(NAME ${name}
        COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}" --target ${target} --config $<CONFIG>
    )
    # Matching the expected error is enough: it only appears when the build failed for that reason. The test
    # runs ninja on the whole build tree, which two ninja processes must not do at once, so compile-fail tests
    # take turns under `ctest -j` (they failed now and then without the lock).
    set_tests_properties(${name} PROPERTIES
        PASS_REGULAR_EXPRESSION "${arg_EXPECT}"
        RESOURCE_LOCK teserra_build_tree
        LABELS "compile_fail;${arg_DEPENDS}"
    )
endfunction()

function(teserra_add_abort_test name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "SOURCE;EXPECT" "DEPENDS")
    if(arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "teserra_add_abort_test(${name}): unknown arguments: ${arg_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT arg_SOURCE OR NOT arg_EXPECT)
        message(FATAL_ERROR "teserra_add_abort_test(${name}): SOURCE and EXPECT are required")
    endif()

    string(MAKE_C_IDENTIFIER "teserra_abort_${name}" target)
    add_executable(${target} ${arg_SOURCE})
    _teserra_configure_test_target(${target})
    foreach(module IN LISTS arg_DEPENDS)
        if(NOT TARGET teserra_${module})
            message(FATAL_ERROR "teserra_add_abort_test(${name}): unknown module '${module}' in DEPENDS")
        endif()
        target_link_libraries(${target} PRIVATE teserra_${module})
    endforeach()

    add_test(NAME ${name}
        COMMAND "${CMAKE_COMMAND}"
            "-DTESERRA_PROGRAM=$<TARGET_FILE:${target}>"
            "-DTESERRA_EXPECT=${arg_EXPECT}"
            -P "${_teserra_testing_dir}/scripts/ExpectAbort.cmake"
    )
    set_tests_properties(${name} PROPERTIES LABELS "abort;${arg_DEPENDS}")
endfunction()

# Shared test support, linked into every teserra_add_test() executable. An OBJECT library, so that the
# Catch2 listener in it is always linked, although no test refers to it.
set(_teserra_testing_dir "${CMAKE_CURRENT_LIST_DIR}")
add_library(teserra_test_support OBJECT
    "${CMAKE_CURRENT_LIST_DIR}/../tests/support/src/allocation_counter.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../tests/support/src/gpu.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../tests/support/src/test_support.cpp"
)
_teserra_configure_test_target(teserra_test_support)
target_include_directories(teserra_test_support PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../tests/support/include")
# teserra_core is defined later (engine/core); target names are resolved when the build is generated.
target_link_libraries(teserra_test_support PUBLIC teserra_core Catch2::Catch2)

# What makes a smoke run of an example or a game skip instead of fail: the machine has no window system (no
# desktop session, no DISPLAY or WAYLAND_DISPLAY: an SSH shell, a container) or no OpenGL 4.5 driver. The
# programs log either at error level, which their FAIL_REGULAR_EXPRESSION would otherwise catch; CTest gives
# the skip expression precedence. CI requires every test to run, so a skip there still fails the job.
set(TESERRA_SMOKE_SKIP_REGEX "OpenGL 4[.]5 core context|Cannot initialize GLFW")

# teserra_add_smoke_run(<name> TARGET <target> FRAMES <count>)
#
# Smoke runs of an example or a game (docs/guides/testing-strategy.md#example-smoke-runs). examples.<name>
# runs <target> for <count> frames in a hidden window from its build directory; it must exit with 0 and log
# nothing at Error or above, which includes OpenGL debug errors and RHI validation. With
# TESERRA_WITH_DEBUG_UI, examples.<name>.debug_ui does the same with the debug UI open (--debug-ui). Machines
# without a window system or an OpenGL 4.5 driver report them skipped (TESERRA_SMOKE_SKIP_REGEX). They run
# with --no-audio, so a test run plays no sound on a developer's machine; the mix runs offline all the same.
function(teserra_add_smoke_run name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "TARGET;FRAMES" "")
    if(NOT arg_TARGET OR NOT arg_FRAMES)
        message(FATAL_ERROR "teserra_add_smoke_run(${name}): TARGET and FRAMES are required")
    endif()
    add_test(NAME examples.${name}
             COMMAND ${arg_TARGET} --frames ${arg_FRAMES} --hidden-window --no-audio
             WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set(runs examples.${name})
    if(TESERRA_WITH_DEBUG_UI)
        add_test(NAME examples.${name}.debug_ui
                 COMMAND ${arg_TARGET} --frames ${arg_FRAMES} --hidden-window --no-audio --debug-ui
                 WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
        list(APPEND runs examples.${name}.debug_ui)
    endif()
    set_tests_properties(${runs} PROPERTIES
        # "[error]" / "[critical]" level tags; [[] matches a literal '[' without escaping.
        FAIL_REGULAR_EXPRESSION "[[](error|critical)]"
        SKIP_REGULAR_EXPRESSION "${TESERRA_SMOKE_SKIP_REGEX}"
        LABELS "examples"
    )
endfunction()
