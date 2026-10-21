#include <teserra/core/config.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string_view>

namespace {

constexpr int kPlatformMacroCount = 0
#if defined(TESERRA_PLATFORM_WINDOWS)
                                    + 1
#endif
#if defined(TESERRA_PLATFORM_LINUX)
                                    + 1
#endif
#if defined(TESERRA_PLATFORM_MACOS)
                                    + 1
#endif
    ;

constexpr int kCompilerMacroCount = 0
#if defined(TESERRA_COMPILER_MSVC)
                                    + 1
#endif
#if defined(TESERRA_COMPILER_CLANG_CL)
                                    + 1
#endif
#if defined(TESERRA_COMPILER_CLANG)
                                    + 1
#endif
#if defined(TESERRA_COMPILER_GCC)
                                    + 1
#endif
    ;

constexpr std::string_view platformName() {
#if defined(TESERRA_PLATFORM_WINDOWS)
    return "Windows";
#elif defined(TESERRA_PLATFORM_LINUX)
    return "Linux";
#elif defined(TESERRA_PLATFORM_MACOS)
    return "Darwin";
#endif
}

constexpr std::string_view compilerName() {
#if defined(TESERRA_COMPILER_MSVC)
    return "MSVC";
#elif defined(TESERRA_COMPILER_CLANG_CL)
    return "ClangCl";
#elif defined(TESERRA_COMPILER_CLANG)
    return "Clang";
#elif defined(TESERRA_COMPILER_GCC)
    return "GCC";
#endif
}

} // namespace

TEST_CASE("config: exactly one platform and one compiler macro is defined", "[core][config]") {
    STATIC_CHECK(kPlatformMacroCount == 1);
    STATIC_CHECK(kCompilerMacroCount == 1);
}

// The expected names come from CMake's own detection (CMAKE_SYSTEM_NAME, CMAKE_CXX_COMPILER_ID and
// the frontend variant), so this cross-checks config.hpp against an independent source.
TEST_CASE("config: detected platform and compiler agree with CMake", "[core][config]") {
    CHECK(platformName() == TESERRA_TEST_EXPECTED_PLATFORM);
    CHECK(compilerName() == TESERRA_TEST_EXPECTED_COMPILER);
}

TEST_CASE("config: debug build macro follows NDEBUG", "[core][config]") {
#if defined(NDEBUG)
    STATIC_CHECK(TESERRA_DEBUG_BUILD == 0);
#else
    STATIC_CHECK(TESERRA_DEBUG_BUILD == 1);
#endif
}
