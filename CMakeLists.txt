cmake_minimum_required(VERSION 3.28)

# Optional dependencies are vcpkg manifest features mapped to CMake options (ADR-0003). The vcpkg
# toolchain installs the manifest during project(), so features are selected before it. Defaults match
# the options below for a top-level build; when Teserra is a subproject, its manifest is not used.
if(NOT DEFINED TESERRA_BUILD_TESTS OR TESERRA_BUILD_TESTS)
    list(APPEND VCPKG_MANIFEST_FEATURES "tests")
endif()
if(TESERRA_SHADER_VALIDATION)
    list(APPEND VCPKG_MANIFEST_FEATURES "shader-validation")
endif()
if(TESERRA_ENABLE_PROFILING)
    list(APPEND VCPKG_MANIFEST_FEATURES "profiling")
endif()
if(TESERRA_BUILD_BENCHMARKS)
    list(APPEND VCPKG_MANIFEST_FEATURES "benchmarks")
endif()
if((NOT DEFINED TESERRA_WITH_DEBUG_UI OR TESERRA_WITH_DEBUG_UI) AND NOT TESERRA_PORTABLE_ONLY)
    list(APPEND VCPKG_MANIFEST_FEATURES "debug-ui")
endif()
# The portable build needs none of the libraries of the default "engine" feature.
if(TESERRA_PORTABLE_ONLY)
    set(VCPKG_MANIFEST_NO_DEFAULT_FEATURES ON)
endif()

project(Teserra
    VERSION 0.1.0
    DESCRIPTION "A modern C++20 game engine for indie developers"
    HOMEPAGE_URL "https://github.com/fyernald/teserra"
    # C only for the vendored glad loader of the OpenGL backend (ADR-0009); engine code is C++.
    LANGUAGES C CXX
)

# ISO C++20 without compiler extensions and without C++20 modules (ADR-0002). These initialize the
# properties of every target defined below; teserra_add_module() also sets them explicitly.
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(TeserraWarnings)
include(TeserraModule)
include(TeserraShaders)
include(TeserraAssets)

# Options (docs/guides/build-and-ci.md#options). Each option is added together with the feature it
# controls. Tests and examples are only built by default when Teserra is the top-level project, not
# when it is consumed through add_subdirectory().
option(TESERRA_BUILD_TESTS "Build unit, integration and GPU test executables" ${PROJECT_IS_TOP_LEVEL})
option(TESERRA_BUILD_EXAMPLES "Build the examples in examples/" ${PROJECT_IS_TOP_LEVEL})
# Catch2 micro-benchmarks in benchmarks/ (docs/architecture/memory-and-performance.md#benchmarking-policy); build
# them in Release, with the windows-msvc-bench preset.
option(TESERRA_BUILD_BENCHMARKS "Build the micro-benchmarks in benchmarks/" OFF)
# Only the modules without a window system or GPU code: core, ecs, scene, rhi with the null backend, and the
# platform without its window backend (with its OS services where the OS has them). The Linux CI job builds
# this while the Linux port is under way (#68, #127, ADR-0015).
option(TESERRA_PORTABLE_ONLY "Build only core, ecs, scene, rhi + null backend and platform without a window backend" OFF)
if(TESERRA_PORTABLE_ONLY)
    set(TESERRA_BUILD_EXAMPLES OFF)
    set(TESERRA_RHI_OPENGL OFF)
endif()
option(TESERRA_WARNINGS_AS_ERRORS "Treat compiler warnings as errors (/WX, -Werror)" OFF)
set(TESERRA_ENABLE_ASSERTS "DEFAULT" CACHE STRING
    "Check TESERRA_ASSERT: DEFAULT (Debug and RelWithDebInfo builds), ON or OFF")
set_property(CACHE TESERRA_ENABLE_ASSERTS PROPERTY STRINGS DEFAULT ON OFF)
option(TESERRA_RHI_OPENGL "Build the OpenGL 4.5 RHI backend (teserra_rhi_opengl)" ON)
option(TESERRA_WITH_DEBUG_DRAW "Compile debug draw (renderer::DebugDrawList); OFF makes its calls empty" ON)
# The in-game debug tools with Dear ImGui (teserra_debug_ui, ADR-0013). OFF builds the engine and games with no
# ImGui code; a game that fetches Teserra with it ON lists imgui in its own vcpkg.json (the template does).
option(TESERRA_WITH_DEBUG_UI "Build the Dear ImGui debug tools (teserra_debug_ui)" ON)
if(TESERRA_PORTABLE_ONLY)
    set(TESERRA_WITH_DEBUG_UI OFF)
endif()
# Tracy behind the TESERRA_PROFILE_* macros (ADR-0023); the profile presets turn it on. OFF builds do not
# link Tracy at all.
option(TESERRA_ENABLE_PROFILING "Profile with Tracy through the TESERRA_PROFILE_* macros (ADR-0023)" OFF)
# AddressSanitizer (and UBSan on Clang/GCC) for every target, for the asan presets and the sanitizer job (#113).
option(TESERRA_ENABLE_SANITIZERS "Build with AddressSanitizer (MSVC) or ASan + UBSan (Clang, GCC)" OFF)
option(TESERRA_SHADER_VALIDATION
    "Validate the SPIR-V of teserra_shaderc in its tests with SPIRV-Tools (slow to build; on in CI)" OFF)

include(TeserraSanitizers)

if(TESERRA_BUILD_TESTS)
    enable_testing()
    include(TeserraTesting)
endif()

# Host tools that the build runs: the shader compiler (ADR-0017). It links teserra_rhi, defined below, for
# the .tshader format.
if(NOT TESERRA_PORTABLE_ONLY)
    add_subdirectory(tools/shaderc)
endif()

# Engine modules, in layer order (docs/architecture/overview.md#layers). A module can only depend on
# modules that were added before it.
add_subdirectory(engine/core)
add_subdirectory(engine/platform)
add_subdirectory(engine/rhi)
if(NOT TESERRA_PORTABLE_ONLY)
    add_subdirectory(engine/assets)
    add_subdirectory(engine/renderer)
endif()
add_subdirectory(engine/ecs)
add_subdirectory(engine/scene)
if(TESERRA_WITH_DEBUG_UI)
    add_subdirectory(engine/debug_ui)
endif()
if(NOT TESERRA_PORTABLE_ONLY)
    add_subdirectory(engine/engine)
endif()

if(TESERRA_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

if(TESERRA_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(TESERRA_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(PROJECT_IS_TOP_LEVEL)
    include(TeserraChecks)
endif()
