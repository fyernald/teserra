# Test helpers (docs/guides/testing-strategy.md, ADR-0012).
#
#   teserra_add_test(<name> SOURCES <file>... [DEPENDS <module>...] [LIBRARIES <target>...])
#
# Creates the Catch2 test executable teserra_<name>_tests. Module unit tests use the module name
# (teserra_add_test(core ...) -> teserra_core_tests). DEPENDS links Teserra modules, LIBRARIES any
# other target. Every TEST_CASE is registered as its own CTest test, labeled <name>, so `ctest -N`,
# IDE test explorers and the JUnit report list individual test cases.

include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/TeserraWarnings.cmake")

find_package(Catch2 3 CONFIG REQUIRED)
include(Catch)

function(teserra_add_test name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES;DEPENDS;LIBRARIES")
    if(arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "teserra_add_test(${name}): unknown arguments: ${arg_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT arg_SOURCES)
        message(FATAL_ERROR "teserra_add_test(${name}): SOURCES is required")
    endif()

    set(target teserra_${name}_tests)
    add_executable(${target} ${arg_SOURCES})
    set_target_properties(${target} PROPERTIES
        CXX_EXTENSIONS OFF
        CXX_SCAN_FOR_MODULES OFF
    )
    target_compile_features(${target} PRIVATE cxx_std_20)
    teserra_set_warnings(${target})

    foreach(module IN LISTS arg_DEPENDS)
        if(NOT TARGET teserra_${module})
            message(FATAL_ERROR "teserra_add_test(${name}): unknown module '${module}' in DEPENDS")
        endif()
        target_link_libraries(${target} PRIVATE teserra_${module})
    endforeach()
    target_link_libraries(${target} PRIVATE Catch2::Catch2WithMain ${arg_LIBRARIES})

    # PRE_TEST discovers test cases when CTest runs instead of after the build, so the build does not
    # execute test binaries and runtime DLLs deployed after linking are in place.
    catch_discover_tests(${target}
        DISCOVERY_MODE PRE_TEST
        PROPERTIES LABELS ${name}
    )
endfunction()
