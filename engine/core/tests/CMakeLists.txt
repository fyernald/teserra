teserra_add_test(core
    SOURCES
        config_test.cpp
        log_level_min_test.cpp
        log_test.cpp
        profile_test.cpp
        version_test.cpp
    DEPENDS
        core
)

# Expectations for config_test.cpp from CMake's own platform and compiler detection, so the test
# cross-checks <teserra/core/config.hpp> against an independent source.
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set(expected_compiler "MSVC")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    set(expected_compiler "ClangCl")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(expected_compiler "Clang")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(expected_compiler "GCC")
else()
    set(expected_compiler "${CMAKE_CXX_COMPILER_ID}")
endif()

target_compile_definitions(teserra_core_tests PRIVATE
    TESERRA_TEST_EXPECTED_PLATFORM="${CMAKE_SYSTEM_NAME}"
    TESERRA_TEST_EXPECTED_COMPILER="${expected_compiler}"
    TESERRA_TEST_PROJECT_VERSION="${PROJECT_VERSION}"
)

# Every public header compiles on its own and keeps spdlog and fmt private.
teserra_add_header_check(core)

# Format strings are checked at compile time (ADR-0011). Every supported compiler names
# std::basic_format_string in the error.
teserra_add_compile_fail_test(core.log.format_string_is_checked
    SOURCE compile_fail/log_format_mismatch.cpp
    EXPECT "basic_format_string"
    DEPENDS core
)
teserra_add_compile_fail_test(core.log.removed_call_format_string_is_checked
    SOURCE compile_fail/log_removed_call_format_mismatch.cpp
    EXPECT "basic_format_string"
    DEPENDS core
)
