#include <teserra/platform/mouse_button.hpp>

#include <teserra/core/assert.hpp>

namespace teserra::platform {

std::string_view mouseButtonName(MouseButton button) noexcept {
    switch (button) {
        case MouseButton::Left:
            return "Left";
        case MouseButton::Right:
            return "Right";
        case MouseButton::Middle:
            return "Middle";
        case MouseButton::Back:
            return "Back";
        case MouseButton::Forward:
            return "Forward";
    }
    TESERRA_UNREACHABLE("Invalid MouseButton value {}", static_cast<usize>(button));
}

} // namespace teserra::platform
