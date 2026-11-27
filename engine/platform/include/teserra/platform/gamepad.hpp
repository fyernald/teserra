#pragma once

// Gamepads with the standard layout (docs/architecture/platform-abstraction.md#gamepads): an Xbox-style
// controller with two sticks, two triggers, four face buttons, two bumpers and a d-pad. The window backend
// maps each supported controller onto it.

#include <teserra/core/types.hpp>

#include <string_view>

namespace teserra::platform {

/// Gamepads tracked at once; Gamepad indices are 0 to kMaxGamepads - 1. A gamepad keeps its index while it
/// is connected; the index of a disconnected one is reused by the next one plugged in.
inline constexpr u32 kMaxGamepads = 16;

/// A button of the standard layout. The face buttons are named by position: South is A on an Xbox
/// controller and Cross on a PlayStation one.
enum class GamepadButton : u8 {
    South,
    East,
    West,
    North,
    LeftBumper,
    RightBumper,
    Back,
    Start,
    Guide,
    LeftStick, ///< Pressing the left stick.
    RightStick,
    DpadUp,
    DpadRight,
    DpadDown,
    DpadLeft,
};

/// Number of GamepadButton values; DpadLeft must stay the last enumerator.
inline constexpr usize kGamepadButtonCount = static_cast<usize>(GamepadButton::DpadLeft) + 1;

/// An axis of the standard layout. Stick axes are -1 to 1 with +X right and +Y up (ADR-0024); triggers are
/// 0 (released) to 1 (fully pressed).
enum class GamepadAxis : u8 {
    LeftX,
    LeftY,
    RightX,
    RightY,
    LeftTrigger,
    RightTrigger,
};

/// Number of GamepadAxis values; RightTrigger must stay the last enumerator.
inline constexpr usize kGamepadAxisCount = static_cast<usize>(GamepadAxis::RightTrigger) + 1;

/// One of the two sticks.
enum class GamepadStick : u8 {
    Left,
    Right,
};

/// English name of `button` for logs and debug UI ("South", "DpadUp").
[[nodiscard]] std::string_view gamepadButtonName(GamepadButton button) noexcept;
/// English name of `axis` for logs and debug UI ("LeftX", "RightTrigger").
[[nodiscard]] std::string_view gamepadAxisName(GamepadAxis axis) noexcept;

} // namespace teserra::platform
