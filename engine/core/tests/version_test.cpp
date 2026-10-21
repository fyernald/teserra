#include <teserra/core/version.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>

TEST_CASE("version: library reports the version of its headers", "[core][version]") {
    CHECK(teserra::versionString() == teserra::kVersionString);
}

TEST_CASE("version: string is major.minor.patch of the numeric components", "[core][version]") {
    const std::string expected = std::to_string(teserra::kVersionMajor) + "." +
                                 std::to_string(teserra::kVersionMinor) + "." +
                                 std::to_string(teserra::kVersionPatch);
    CHECK(teserra::kVersionString == expected);
}

TEST_CASE("version: matches the CMake project version", "[core][version]") {
    CHECK(teserra::kVersionString == TESERRA_TEST_PROJECT_VERSION);
}

TEST_CASE("version: deliberately failing test (CI check, never merged)", "[core][version]") {
    CHECK(teserra::kVersionMajor == 99);
}
