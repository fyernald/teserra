// The services a Platform hands out. It needs a window system, so the portable build leaves it out.

#include <teserra/platform/platform.hpp>

#include <teserra/platform/file_system.hpp>

#include "platform_test_support.hpp"
#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace teserra;
using namespace teserra::platform;

TEST_CASE("Platform: fileSystem() is the native file system", "[platform][file_system]") {
    const auto platform = test::createPlatformOrSkip();
    FileSystem& fileSystem = platform->fileSystem();

    const auto directory = fileSystem.executableDirectory();

    REQUIRE(directory.has_value());
    CHECK(fileSystem.exists(*directory + "/" + TESERRA_TEST_EXECUTABLE_NAME));
}
