teserra_add_test(core
    SOURCES
        assert_disabled_test.cpp
        assert_test.cpp
        config_test.cpp
        flags_test.cpp
        handle_test.cpp
        log_level_min_test.cpp
        log_test.cpp
        math_test.cpp
        profile_test.cpp
        result_test.cpp
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
    # The documented default of the TESERRA_ENABLE_ASSERTS option, written out independently of
    # engine/core/CMakeLists.txt.
    "TESERRA_TEST_EXPECTED_ASSERTS=$<IF:$<STREQUAL:${TESERRA_ENABLE_ASSERTS},DEFAULT>,$<IF:$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>,1,0>,$<BOOL:${TESERRA_ENABLE_ASSERTS}>>"
)

# Every public header compiles on its own and keeps private dependencies out of its interface.
teserra_add_header_check(core)

# Format strings are checked at compile time (ADR-0011). Every supported compiler names
# std::basic_format_string in the error.
teserra_add_compile_fail_test(core.log.format_string_is_checked
    SOURCE compile_fail/log_format_mismatch.cpp
    EXPECT "basic_format_string"
    DEPENDS core
)
# A discarded Result is diagnosed (MSVC C4834, Clang and GCC -Wunused-result), an error in CI.
teserra_add_compile_fail_test(core.result.discarding_is_diagnosed
    SOURCE compile_fail/result_discarded.cpp
    EXPECT "nodiscard"
    WARNINGS_AS_ERRORS
    DEPENDS core
)
teserra_add_compile_fail_test(core.log.removed_call_format_string_is_checked
    SOURCE compile_fail/log_removed_call_format_mismatch.cpp
    EXPECT "basic_format_string"
    DEPENDS core
)

# With the default handler, TESERRA_FATAL logs, flushes and aborts (ADR-0016).
teserra_add_abort_test(core.assert.fatal_aborts
    SOURCE fatal_abort.cpp
    EXPECT "Fatal error [(][^)]*fatal_abort[.]cpp:[0-9]+[)]: GPU device lost: driver reset"
    DEPENDS core
)
