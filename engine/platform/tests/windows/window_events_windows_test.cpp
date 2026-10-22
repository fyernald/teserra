// Windows only (selected in CMake): sends the window messages a user's actions produce and checks that
// they arrive as engine events.

#include <teserra/platform/platform.hpp>

#include "platform_test_support.hpp"
#include <catch2/catch_test_macros.hpp>

#define WIN32_LEAN_AND_MEAN // NOLINT(readability-identifier-naming): Windows SDK configuration macro
#define NOMINMAX            // NOLINT(readability-identifier-naming): Windows SDK configuration macro
#include <windows.h>

#include <variant>
#include <vector>

using namespace teserra;
using namespace teserra::platform;

namespace {

HWND hwndOf(const Window& window) {
    const NativeWindowHandle handle = window.nativeHandle();
    REQUIRE(handle.kind == NativeWindowHandle::Kind::Win32);
    return static_cast<HWND>(handle.window);
}

std::vector<Event> poll(Platform& platform) {
    EventQueue queue;
    platform.pollEvents(queue);
    return {queue.begin(), queue.end()};
}

} // namespace

TEST_CASE("Window (Win32): native handle is the HWND and HINSTANCE", "[platform][window]") {
    const auto platform = test::createPlatformOrSkip();
    const auto window = platform->createWindow(test::hiddenWindow());
    REQUIRE(window.has_value());

    const NativeWindowHandle handle = (*window)->nativeHandle();
    CHECK(handle.kind == NativeWindowHandle::Kind::Win32);
    CHECK(IsWindow(static_cast<HWND>(handle.window)) != FALSE);
    CHECK(handle.display == GetModuleHandleW(nullptr));
}

TEST_CASE("Window (Win32): WM_CLOSE becomes WindowCloseRequested", "[platform][window]") {
    const auto platform = test::createPlatformOrSkip();
    const auto window = platform->createWindow(test::hiddenWindow());
    REQUIRE(window.has_value());
    static_cast<void>(poll(*platform));

    SendMessageW(hwndOf(**window), WM_CLOSE, 0, 0);

    CHECK(poll(*platform) == std::vector<Event>{WindowCloseRequested{}});
    CHECK((*window)->shouldClose());
}

TEST_CASE("Window (Win32): WM_SETFOCUS and WM_KILLFOCUS become FocusChanged", "[platform][window]") {
    const auto platform = test::createPlatformOrSkip();
    const auto window = platform->createWindow(test::hiddenWindow());
    REQUIRE(window.has_value());
    static_cast<void>(poll(*platform));
    const HWND hwnd = hwndOf(**window);

    SendMessageW(hwnd, WM_SETFOCUS, 0, 0);
    SendMessageW(hwnd, WM_KILLFOCUS, 0, 0);

    CHECK(poll(*platform) == std::vector<Event>{FocusChanged{true}, FocusChanged{false}});
}
