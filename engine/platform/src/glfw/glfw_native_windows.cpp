// Native handles of GLFW windows on Windows. GLFW's native access header needs <windows.h>, so this is
// the one file where a GLFW header and an OS header meet; CMake compiles it only for the GLFW backend on
// Windows (docs/architecture/platform-abstraction.md#directory-rules).

#include "glfw/glfw_native.hpp"

#define WIN32_LEAN_AND_MEAN // NOLINT(readability-identifier-naming): Windows SDK configuration macro
#define NOMINMAX            // NOLINT(readability-identifier-naming): Windows SDK configuration macro
#include <windows.h>

#define GLFW_EXPOSE_NATIVE_WIN32 // NOLINT(readability-identifier-naming): GLFW configuration macro
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

namespace teserra::platform {

NativeWindowHandle nativeHandleOf(GLFWwindow* window) noexcept {
    return {NativeWindowHandle::Kind::Win32, glfwGetWin32Window(window), GetModuleHandleW(nullptr)};
}

} // namespace teserra::platform
