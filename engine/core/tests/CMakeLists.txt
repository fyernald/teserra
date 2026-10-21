teserra_add_test(core
    SOURCES
        config_test.cpp
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
