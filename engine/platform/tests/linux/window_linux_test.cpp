// Native window handles on Linux: GLFW runs on Wayland or X11, and the handle says which, with the
// session's display connection (window.hpp).

#include <teserra/platform/platform.hpp>
#include <teserra/platform/window.hpp>

#include "platform_test_support.hpp"
#include <catch2/catch_test_macros.hpp>

#include <memory>

using namespace teserra;
using namespace teserra::platform;

TEST_CASE("Window (Linux): the native handle is an X11 or a Wayland window with its display",
          "[platform][window]") {
    const auto platform = test::createPlatformOrSkip();
    const Result<std::unique_ptr<Window>> created = platform->createWindow(test::hiddenWindow());
    REQUIRE(created.has_value());

    const NativeWindowHandle handle = (*created)->nativeHandle();

    const bool x11OrWayland =
        handle.kind == NativeWindowHandle::Kind::X11 || handle.kind == NativeWindowHandle::Kind::Wayland;
    CHECK(x11OrWayland);
    CHECK(handle.window != nullptr);
    CHECK(handle.display != nullptr);
}
