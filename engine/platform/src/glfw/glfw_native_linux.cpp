// Native handles of GLFW windows on Linux, on X11 or Wayland: GLFW picks one when it is initialized
// (glfwGetPlatform()). GLFW's native access header needs the X11 and Wayland client headers, so this is the
// one file where they meet GLFW; CMake compiles it only for the GLFW backend on Linux
// (docs/architecture/platform-abstraction.md#directory-rules).
//
// <X11/Xlib.h> defines macros such as None, Bool and Status, so this file names no engine identifier
// spelled like them (an empty NativeWindowHandle is value-initialized instead of Kind::None).

#include "glfw/glfw_native.hpp"

#define GLFW_EXPOSE_NATIVE_X11     // NOLINT(readability-identifier-naming): GLFW configuration macro
#define GLFW_EXPOSE_NATIVE_WAYLAND // NOLINT(readability-identifier-naming): GLFW configuration macro
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <cstdint>

namespace teserra::platform {

NativeWindowHandle nativeHandleOf(GLFWwindow* window) noexcept {
    switch (glfwGetPlatform()) {
        case GLFW_PLATFORM_X11: {
            // An X11 Window is an integer id; the handle carries it in the pointer (window.hpp).
            const auto id = static_cast<std::uintptr_t>(glfwGetX11Window(window));
            void* const handle = reinterpret_cast<void*>(id); // NOLINT(performance-no-int-to-ptr)
            return {NativeWindowHandle::Kind::X11, handle, glfwGetX11Display()};
        }
        case GLFW_PLATFORM_WAYLAND:
            return {NativeWindowHandle::Kind::Wayland, glfwGetWaylandWindow(window), glfwGetWaylandDisplay()};
        default:
            return NativeWindowHandle{};
    }
}

} // namespace teserra::platform
