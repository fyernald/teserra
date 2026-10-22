// Windows only (selected in CMake): sends the window messages a user's actions produce and checks that
// they arrive as engine events.

#include <teserra/platform/input.hpp>
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

namespace {

// lParam of WM_KEYDOWN/WM_KEYUP: scancode in bits 16-23, extended-key flag in bit 24, and for key-up the
// previous-state and transition bits 30 and 31.
LPARAM keyLParam(UINT scancode, bool extended, bool up) {
    auto bits = static_cast<DWORD>(1); // repeat count
    bits |= (scancode & 0xFFU) << 16U;
    bits |= extended ? (1U << 24U) : 0U;
    bits |= up ? ((1U << 30U) | (1U << 31U)) : 0U;
    return static_cast<LPARAM>(bits);
}

void sendKey(HWND hwnd, WPARAM virtualKey, UINT scancode, bool extended, bool up) {
    SendMessageW(hwnd, up ? WM_KEYUP : WM_KEYDOWN, virtualKey, keyLParam(scancode, extended, up));
}

KeyChanged keyEvent(Key key, bool down, bool repeat = false) {
    return {.key = key, .down = down, .repeat = repeat};
}

} // namespace

TEST_CASE("Window (Win32): key messages become KeyChanged by physical key", "[platform][input]") {
    const auto platform = test::createPlatformOrSkip();
    const auto window = platform->createWindow(test::hiddenWindow());
    REQUIRE(window.has_value());
    static_cast<void>(poll(*platform));
    const HWND hwnd = hwndOf(**window);

    // Scancode 0x11 is the key above S. On AZERTY it types Z, but it is still Key::W.
    sendKey(hwnd, 'Z', 0x11, false, false);
    sendKey(hwnd, 'Z', 0x11, false, false); // held: the OS repeats key-down
    sendKey(hwnd, 'Z', 0x11, false, true);
    // 0x4B is Numpad 4, and Left with the extended flag.
    sendKey(hwnd, VK_NUMPAD4, 0x4B, false, false);
    sendKey(hwnd, VK_LEFT, 0x4B, true, false);
    sendKey(hwnd, VK_ESCAPE, 0x01, false, false);

    CHECK(poll(*platform) == std::vector<Event>{
                                 keyEvent(Key::W, true),
                                 keyEvent(Key::W, true, true),
                                 keyEvent(Key::W, false),
                                 keyEvent(Key::Numpad4, true),
                                 keyEvent(Key::Left, true),
                                 keyEvent(Key::Escape, true),
                             });
}

TEST_CASE("Window (Win32): WM_CHAR becomes TextEntered, including surrogate pairs", "[platform][input]") {
    const auto platform = test::createPlatformOrSkip();
    const auto window = platform->createWindow(test::hiddenWindow());
    REQUIRE(window.has_value());
    static_cast<void>(poll(*platform));
    const HWND hwnd = hwndOf(**window);

    SendMessageW(hwnd, WM_CHAR, L'a', 0);
    SendMessageW(hwnd, WM_CHAR, 0x00E9, 0); // e acute
    SendMessageW(hwnd, WM_CHAR, 0xD83D, 0); // U+1F600 as UTF-16
    SendMessageW(hwnd, WM_CHAR, 0xDE00, 0);

    CHECK(poll(*platform) ==
          std::vector<Event>{TextEntered{U'a'}, TextEntered{U'é'}, TextEntered{U'\U0001F600'}});
}

TEST_CASE("Window (Win32): mouse messages become button, move and scroll events", "[platform][input]") {
    const auto platform = test::createPlatformOrSkip();
    const auto window = platform->createWindow(test::hiddenWindow());
    REQUIRE(window.has_value());
    static_cast<void>(poll(*platform));
    const HWND hwnd = hwndOf(**window);

    SendMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(40, 30));
    SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(40, 30));
    SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(40, 30));
    SendMessageW(hwnd, WM_XBUTTONDOWN, MAKEWPARAM(MK_XBUTTON1, XBUTTON1), MAKELPARAM(40, 30));
    SendMessageW(hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-2 * WHEEL_DELTA)), 0);

    CHECK(poll(*platform) == std::vector<Event>{
                                 MouseMoved{{40.0F, 30.0F}},
                                 MouseButtonChanged{MouseButton::Left, true},
                                 MouseButtonChanged{MouseButton::Left, false},
                                 MouseButtonChanged{MouseButton::Back, true},
                                 MouseScrolled{{0.0F, -2.0F}},
                             });
}

TEST_CASE("Window (Win32): focus loss releases held keys through Input", "[platform][input]") {
    const auto platform = test::createPlatformOrSkip();
    const auto window = platform->createWindow(test::hiddenWindow());
    REQUIRE(window.has_value());
    static_cast<void>(poll(*platform));
    const HWND hwnd = hwndOf(**window);
    Input input;
    const auto frame = [&] {
        input.beginFrame();
        for (const Event& event : poll(*platform)) {
            input.processEvent(event);
        }
    };

    sendKey(hwnd, 'W', 0x11, false, false);
    frame();
    REQUIRE(input.isDown(Key::W));

    SendMessageW(hwnd, WM_KILLFOCUS, 0, 0);
    frame();

    CHECK_FALSE(input.isDown(Key::W));
    CHECK(input.wasReleased(Key::W));
}
