#pragma once

// Helpers for tests that need a real window system.

#include <teserra/platform/platform.hpp>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>

namespace teserra::test {

/// Creates the platform, or skips the test when the machine has no window system (no desktop session).
inline std::unique_ptr<platform::Platform> createPlatformOrSkip() {
    Result<std::unique_ptr<platform::Platform>> platform = platform::Platform::create();
    if (!platform) {
        REQUIRE(platform.error().code == ErrorCode::DeviceFailure);
        SKIP("No window system: " << std::format("{}", platform.error()));
    }
    return std::move(platform).value();
}

/// A small hidden window without a graphics context.
inline platform::WindowDesc hiddenWindow(platform::GraphicsApi api = platform::GraphicsApi::None) {
    platform::WindowDesc desc;
    desc.title = "Teserra test window";
    desc.size = {320, 200};
    desc.visible = false;
    desc.graphicsApi = api;
    return desc;
}

} // namespace teserra::test
