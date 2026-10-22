#pragma once

#include <teserra/platform/key.hpp>
#include <teserra/platform/mouse_button.hpp>

#include <optional>

namespace teserra::platform {

/// The engine key for a GLFW key code (GLFW_KEY_*), or Key::Unknown for GLFW_KEY_UNKNOWN and keys the
/// engine does not name (F25).
[[nodiscard]] Key keyFromGlfw(int glfwKey) noexcept;

/// The engine button for a GLFW mouse button (GLFW_MOUSE_BUTTON_*), or nothing for buttons 6 to 8.
[[nodiscard]] std::optional<MouseButton> mouseButtonFromGlfw(int glfwButton) noexcept;

} // namespace teserra::platform
