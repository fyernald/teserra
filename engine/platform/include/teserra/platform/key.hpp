#pragma once

#include <teserra/core/types.hpp>

#include <string_view>

namespace teserra::platform {

/// A key by its *physical* position, named after the key at that position on a US keyboard: Key::W is the
/// key above S whatever the layout prints on it, so WASD controls work on AZERTY too. Text goes through
/// TextEntered events instead (docs/architecture/platform-abstraction.md#interfaces).
enum class Key : u8 {
    Unknown, ///< A key the window backend cannot identify; never reported in events.

    A,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,

    Digit0,
    Digit1,
    Digit2,
    Digit3,
    Digit4,
    Digit5,
    Digit6,
    Digit7,
    Digit8,
    Digit9,

    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
    F13,
    F14,
    F15,
    F16,
    F17,
    F18,
    F19,
    F20,
    F21,
    F22,
    F23,
    F24,

    Escape,
    Enter,
    Tab,
    Backspace,
    Space,
    Insert,
    Delete,
    Home,
    End,
    PageUp,
    PageDown,
    Left,
    Right,
    Up,
    Down,
    CapsLock,
    ScrollLock,
    NumLock,
    PrintScreen,
    Pause,
    Menu,

    Apostrophe,   ///< ' on a US keyboard
    Comma,        ///< ,
    Minus,        ///< -
    Period,       ///< .
    Slash,        ///< /
    Semicolon,    ///< ;
    Equal,        ///< =
    LeftBracket,  ///< [
    Backslash,    ///< (backslash)
    RightBracket, ///< ]
    GraveAccent,  ///< `
    World1,       ///< Extra key of some non-US keyboards (GLFW "non-US #1")
    World2,       ///< Extra key of some non-US keyboards, e.g. the ISO key left of Z (GLFW "non-US #2")

    Numpad0,
    Numpad1,
    Numpad2,
    Numpad3,
    Numpad4,
    Numpad5,
    Numpad6,
    Numpad7,
    Numpad8,
    Numpad9,
    NumpadDecimal,
    NumpadDivide,
    NumpadMultiply,
    NumpadSubtract,
    NumpadAdd,
    NumpadEnter,
    NumpadEqual,

    LeftShift,
    LeftControl,
    LeftAlt,
    LeftSuper, ///< Super: the Windows key
    RightShift,
    RightControl,
    RightAlt,
    RightSuper,
};

/// Number of Key values, including Key::Unknown; RightSuper must stay the last enumerator.
inline constexpr usize kKeyCount = static_cast<usize>(Key::RightSuper) + 1;

/// English name of `key` for logs and debug UI ("W", "Left Shift", "Numpad 5"), not localized and not
/// layout-aware.
[[nodiscard]] std::string_view keyName(Key key) noexcept;

} // namespace teserra::platform
