// The only place GLFW key and mouse button codes are translated (docs/architecture/platform-abstraction.md).
// GLFW key codes name physical keys by their US-layout meaning, like Key.

#include "glfw/glfw_key_table.hpp"

#include <GLFW/glfw3.h>

#include <array>

namespace teserra::platform {

namespace {

struct KeyMapping {
    int glfwKey;
    Key key;
};

constexpr std::array kKeyMappings{
    KeyMapping{GLFW_KEY_A, Key::A},
    KeyMapping{GLFW_KEY_B, Key::B},
    KeyMapping{GLFW_KEY_C, Key::C},
    KeyMapping{GLFW_KEY_D, Key::D},
    KeyMapping{GLFW_KEY_E, Key::E},
    KeyMapping{GLFW_KEY_F, Key::F},
    KeyMapping{GLFW_KEY_G, Key::G},
    KeyMapping{GLFW_KEY_H, Key::H},
    KeyMapping{GLFW_KEY_I, Key::I},
    KeyMapping{GLFW_KEY_J, Key::J},
    KeyMapping{GLFW_KEY_K, Key::K},
    KeyMapping{GLFW_KEY_L, Key::L},
    KeyMapping{GLFW_KEY_M, Key::M},
    KeyMapping{GLFW_KEY_N, Key::N},
    KeyMapping{GLFW_KEY_O, Key::O},
    KeyMapping{GLFW_KEY_P, Key::P},
    KeyMapping{GLFW_KEY_Q, Key::Q},
    KeyMapping{GLFW_KEY_R, Key::R},
    KeyMapping{GLFW_KEY_S, Key::S},
    KeyMapping{GLFW_KEY_T, Key::T},
    KeyMapping{GLFW_KEY_U, Key::U},
    KeyMapping{GLFW_KEY_V, Key::V},
    KeyMapping{GLFW_KEY_W, Key::W},
    KeyMapping{GLFW_KEY_X, Key::X},
    KeyMapping{GLFW_KEY_Y, Key::Y},
    KeyMapping{GLFW_KEY_Z, Key::Z},
    KeyMapping{GLFW_KEY_0, Key::Digit0},
    KeyMapping{GLFW_KEY_1, Key::Digit1},
    KeyMapping{GLFW_KEY_2, Key::Digit2},
    KeyMapping{GLFW_KEY_3, Key::Digit3},
    KeyMapping{GLFW_KEY_4, Key::Digit4},
    KeyMapping{GLFW_KEY_5, Key::Digit5},
    KeyMapping{GLFW_KEY_6, Key::Digit6},
    KeyMapping{GLFW_KEY_7, Key::Digit7},
    KeyMapping{GLFW_KEY_8, Key::Digit8},
    KeyMapping{GLFW_KEY_9, Key::Digit9},
    KeyMapping{GLFW_KEY_F1, Key::F1},
    KeyMapping{GLFW_KEY_F2, Key::F2},
    KeyMapping{GLFW_KEY_F3, Key::F3},
    KeyMapping{GLFW_KEY_F4, Key::F4},
    KeyMapping{GLFW_KEY_F5, Key::F5},
    KeyMapping{GLFW_KEY_F6, Key::F6},
    KeyMapping{GLFW_KEY_F7, Key::F7},
    KeyMapping{GLFW_KEY_F8, Key::F8},
    KeyMapping{GLFW_KEY_F9, Key::F9},
    KeyMapping{GLFW_KEY_F10, Key::F10},
    KeyMapping{GLFW_KEY_F11, Key::F11},
    KeyMapping{GLFW_KEY_F12, Key::F12},
    KeyMapping{GLFW_KEY_F13, Key::F13},
    KeyMapping{GLFW_KEY_F14, Key::F14},
    KeyMapping{GLFW_KEY_F15, Key::F15},
    KeyMapping{GLFW_KEY_F16, Key::F16},
    KeyMapping{GLFW_KEY_F17, Key::F17},
    KeyMapping{GLFW_KEY_F18, Key::F18},
    KeyMapping{GLFW_KEY_F19, Key::F19},
    KeyMapping{GLFW_KEY_F20, Key::F20},
    KeyMapping{GLFW_KEY_F21, Key::F21},
    KeyMapping{GLFW_KEY_F22, Key::F22},
    KeyMapping{GLFW_KEY_F23, Key::F23},
    KeyMapping{GLFW_KEY_F24, Key::F24},
    KeyMapping{GLFW_KEY_ESCAPE, Key::Escape},
    KeyMapping{GLFW_KEY_ENTER, Key::Enter},
    KeyMapping{GLFW_KEY_TAB, Key::Tab},
    KeyMapping{GLFW_KEY_BACKSPACE, Key::Backspace},
    KeyMapping{GLFW_KEY_SPACE, Key::Space},
    KeyMapping{GLFW_KEY_INSERT, Key::Insert},
    KeyMapping{GLFW_KEY_DELETE, Key::Delete},
    KeyMapping{GLFW_KEY_HOME, Key::Home},
    KeyMapping{GLFW_KEY_END, Key::End},
    KeyMapping{GLFW_KEY_PAGE_UP, Key::PageUp},
    KeyMapping{GLFW_KEY_PAGE_DOWN, Key::PageDown},
    KeyMapping{GLFW_KEY_LEFT, Key::Left},
    KeyMapping{GLFW_KEY_RIGHT, Key::Right},
    KeyMapping{GLFW_KEY_UP, Key::Up},
    KeyMapping{GLFW_KEY_DOWN, Key::Down},
    KeyMapping{GLFW_KEY_CAPS_LOCK, Key::CapsLock},
    KeyMapping{GLFW_KEY_SCROLL_LOCK, Key::ScrollLock},
    KeyMapping{GLFW_KEY_NUM_LOCK, Key::NumLock},
    KeyMapping{GLFW_KEY_PRINT_SCREEN, Key::PrintScreen},
    KeyMapping{GLFW_KEY_PAUSE, Key::Pause},
    KeyMapping{GLFW_KEY_MENU, Key::Menu},
    KeyMapping{GLFW_KEY_APOSTROPHE, Key::Apostrophe},
    KeyMapping{GLFW_KEY_COMMA, Key::Comma},
    KeyMapping{GLFW_KEY_MINUS, Key::Minus},
    KeyMapping{GLFW_KEY_PERIOD, Key::Period},
    KeyMapping{GLFW_KEY_SLASH, Key::Slash},
    KeyMapping{GLFW_KEY_SEMICOLON, Key::Semicolon},
    KeyMapping{GLFW_KEY_EQUAL, Key::Equal},
    KeyMapping{GLFW_KEY_LEFT_BRACKET, Key::LeftBracket},
    KeyMapping{GLFW_KEY_BACKSLASH, Key::Backslash},
    KeyMapping{GLFW_KEY_RIGHT_BRACKET, Key::RightBracket},
    KeyMapping{GLFW_KEY_GRAVE_ACCENT, Key::GraveAccent},
    KeyMapping{GLFW_KEY_WORLD_1, Key::World1},
    KeyMapping{GLFW_KEY_WORLD_2, Key::World2},
    KeyMapping{GLFW_KEY_KP_0, Key::Numpad0},
    KeyMapping{GLFW_KEY_KP_1, Key::Numpad1},
    KeyMapping{GLFW_KEY_KP_2, Key::Numpad2},
    KeyMapping{GLFW_KEY_KP_3, Key::Numpad3},
    KeyMapping{GLFW_KEY_KP_4, Key::Numpad4},
    KeyMapping{GLFW_KEY_KP_5, Key::Numpad5},
    KeyMapping{GLFW_KEY_KP_6, Key::Numpad6},
    KeyMapping{GLFW_KEY_KP_7, Key::Numpad7},
    KeyMapping{GLFW_KEY_KP_8, Key::Numpad8},
    KeyMapping{GLFW_KEY_KP_9, Key::Numpad9},
    KeyMapping{GLFW_KEY_KP_DECIMAL, Key::NumpadDecimal},
    KeyMapping{GLFW_KEY_KP_DIVIDE, Key::NumpadDivide},
    KeyMapping{GLFW_KEY_KP_MULTIPLY, Key::NumpadMultiply},
    KeyMapping{GLFW_KEY_KP_SUBTRACT, Key::NumpadSubtract},
    KeyMapping{GLFW_KEY_KP_ADD, Key::NumpadAdd},
    KeyMapping{GLFW_KEY_KP_ENTER, Key::NumpadEnter},
    KeyMapping{GLFW_KEY_KP_EQUAL, Key::NumpadEqual},
    KeyMapping{GLFW_KEY_LEFT_SHIFT, Key::LeftShift},
    KeyMapping{GLFW_KEY_LEFT_CONTROL, Key::LeftControl},
    KeyMapping{GLFW_KEY_LEFT_ALT, Key::LeftAlt},
    KeyMapping{GLFW_KEY_LEFT_SUPER, Key::LeftSuper},
    KeyMapping{GLFW_KEY_RIGHT_SHIFT, Key::RightShift},
    KeyMapping{GLFW_KEY_RIGHT_CONTROL, Key::RightControl},
    KeyMapping{GLFW_KEY_RIGHT_ALT, Key::RightAlt},
    KeyMapping{GLFW_KEY_RIGHT_SUPER, Key::RightSuper},
};

// Every Key except Unknown is reachable from exactly one GLFW key, so adding a Key without a mapping (or
// mapping one twice) fails the build.
constexpr bool mapsEveryKeyOnce() {
    std::array<int, kKeyCount> uses{};
    for (const KeyMapping& mapping : kKeyMappings) {
        ++uses[static_cast<usize>(mapping.key)];
    }
    for (usize index = 1; index < kKeyCount; ++index) {
        if (uses[index] != 1) {
            return false;
        }
    }
    return uses[0] == 0; // Key::Unknown
}
static_assert(mapsEveryKeyOnce(), "kKeyMappings must map every Key except Key::Unknown exactly once");

// Direct lookup by GLFW key code; unmapped codes stay Key::Unknown.
constexpr auto kKeysByGlfwKey = [] {
    std::array<Key, GLFW_KEY_LAST + 1> keys{};
    for (const KeyMapping& mapping : kKeyMappings) {
        keys[static_cast<usize>(mapping.glfwKey)] = mapping.key;
    }
    return keys;
}();

} // namespace

Key keyFromGlfw(int glfwKey) noexcept {
    if (glfwKey < 0 || glfwKey > GLFW_KEY_LAST) {
        return Key::Unknown;
    }
    return kKeysByGlfwKey[static_cast<usize>(glfwKey)];
}

std::optional<MouseButton> mouseButtonFromGlfw(int glfwButton) noexcept {
    switch (glfwButton) {
        case GLFW_MOUSE_BUTTON_LEFT:
            return MouseButton::Left;
        case GLFW_MOUSE_BUTTON_RIGHT:
            return MouseButton::Right;
        case GLFW_MOUSE_BUTTON_MIDDLE:
            return MouseButton::Middle;
        case GLFW_MOUSE_BUTTON_4:
            return MouseButton::Back;
        case GLFW_MOUSE_BUTTON_5:
            return MouseButton::Forward;
        default:
            return std::nullopt;
    }
}

} // namespace teserra::platform
