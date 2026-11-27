#include <teserra/platform/gamepad.hpp>

#include <teserra/core/assert.hpp>

namespace teserra::platform {

std::string_view gamepadButtonName(GamepadButton button) noexcept {
    switch (button) {
        case GamepadButton::South:
            return "South";
        case GamepadButton::East:
            return "East";
        case GamepadButton::West:
            return "West";
        case GamepadButton::North:
            return "North";
        case GamepadButton::LeftBumper:
            return "LeftBumper";
        case GamepadButton::RightBumper:
            return "RightBumper";
        case GamepadButton::Back:
            return "Back";
        case GamepadButton::Start:
            return "Start";
        case GamepadButton::Guide:
            return "Guide";
        case GamepadButton::LeftStick:
            return "LeftStick";
        case GamepadButton::RightStick:
            return "RightStick";
        case GamepadButton::DpadUp:
            return "DpadUp";
        case GamepadButton::DpadRight:
            return "DpadRight";
        case GamepadButton::DpadDown:
            return "DpadDown";
        case GamepadButton::DpadLeft:
            return "DpadLeft";
    }
    TESERRA_UNREACHABLE("Invalid GamepadButton value {}", static_cast<usize>(button));
}

std::string_view gamepadAxisName(GamepadAxis axis) noexcept {
    switch (axis) {
        case GamepadAxis::LeftX:
            return "LeftX";
        case GamepadAxis::LeftY:
            return "LeftY";
        case GamepadAxis::RightX:
            return "RightX";
        case GamepadAxis::RightY:
            return "RightY";
        case GamepadAxis::LeftTrigger:
            return "LeftTrigger";
        case GamepadAxis::RightTrigger:
            return "RightTrigger";
    }
    TESERRA_UNREACHABLE("Invalid GamepadAxis value {}", static_cast<usize>(axis));
}

} // namespace teserra::platform
