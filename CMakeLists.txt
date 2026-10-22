cmake_minimum_required(VERSION 3.28)

# Optional dependencies are vcpkg manifest features mapped to CMake options (ADR-0003). The vcpkg
# toolchain installs the manifest during project(), so features are selected before it. Defaults match
# the options below for a top-level build; when Teserra is a subproject, its manifest is not used.
if(NOT DEFINED TESERRA_BUILD_TESTS OR TESERRA_BUILD_TESTS)
    list(APPEND VCPKG_MANIFEST_FEATURES "tests")
endif()

project(Teserra
    VERSION 0.0.1
    DESCRIPTION "A modern C++20 game engine for indie developers"
    HOMEPAGE_URL "https://github.com/fyernald/teserra"
    LANGUAGES CXX
)

# ISO C++20 without compiler extensions and without C++20 modules (ADR-0002). These initialize the
# properties of every target defined below; teserra_add_module() also sets them explicitly.
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(TeserraWarnings)
include(TeserraModule)

# Options (docs/guides/build-and-ci.md#options). Each option is added together with the feature it
# controls. Tests and examples are only built by default when Teserra is the top-level project, not
# when it is consumed through add_subdirectory().
option(TESERRA_BUILD_TESTS "Build unit, integration and GPU test executables" ${PROJECT_IS_TOP_LEVEL})
option(TESERRA_BUILD_EXAMPLES "Build the examples in examples/" ${PROJECT_IS_TOP_LEVEL})
option(TESERRA_WARNINGS_AS_ERRORS "Treat compiler warnings as errors (/WX, -Werror)" OFF)
set(TESERRA_ENABLE_ASSERTS "DEFAULT" CACHE STRING
    "Check TESERRA_ASSERT: DEFAULT (Debug and RelWithDebInfo builds), ON or OFF")
set_property(CACHE TESERRA_ENABLE_ASSERTS PROPERTY STRINGS DEFAULT ON OFF)

if(TESERRA_BUILD_TESTS)
    enable_testing()
    include(TeserraTesting)
endif()

# Engine modules, in layer order (docs/architecture/overview.md#layers). A module can only depend on
# modules that were added before it.
add_subdirectory(engine/core)

if(TESERRA_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(TESERRA_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(PROJECT_IS_TOP_LEVEL)
    include(TeserraChecks)
endif()
