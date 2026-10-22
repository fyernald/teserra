#pragma once

#include <teserra/platform/window.hpp>

struct GLFWwindow;

namespace teserra::platform {

/// The OS handles of a GLFW window. Implemented once per OS (glfw_native_<os>.cpp, selected in CMake),
/// because reaching them needs GLFW's native access header together with OS headers.
[[nodiscard]] NativeWindowHandle nativeHandleOf(GLFWwindow* window) noexcept;

} // namespace teserra::platform
