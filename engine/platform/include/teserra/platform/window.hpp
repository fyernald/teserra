#pragma once

#include <teserra/core/config.hpp>
#include <teserra/core/math.hpp>
#include <teserra/core/types.hpp>

#include <string>
#include <string_view>

namespace teserra::platform {

class OpenGlContext;

/// Graphics API a window is created for.
enum class GraphicsApi : u8 {
    None,   ///< No context: for Vulkan (future) or windows that are never rendered to.
    OpenGL, ///< An OpenGL 4.5 core context, available through Window::openGlContext().
};

/// Creation parameters of a window.
struct WindowDesc {
    std::string title = "Teserra";
    /// Size in screen coordinates. On HiDPI displays the window is scaled by the monitor's content scale.
    UVec2 size{1280, 720};
    bool resizable = true;
    /// False creates a hidden window, e.g. for tests and headless runs.
    bool visible = true;
    GraphicsApi graphicsApi = GraphicsApi::OpenGL;
    /// Requests an OpenGL debug context (KHR_debug output). Defaults to on in Debug builds.
    bool debugContext = TESERRA_DEBUG_BUILD != 0;
};

/// The operating system's handles of a window, for code that must talk to the OS or a graphics API
/// directly (a future Vulkan backend creating a surface). Opaque pointers, so no OS header is needed.
struct NativeWindowHandle {
    enum class Kind : u8 {
        None,
        Win32,   ///< window: HWND, display: HINSTANCE
        X11,     ///< window: Window (as an integer in the pointer), display: Display* (future)
        Wayland, ///< window: wl_surface*, display: wl_display* (future)
    };

    Kind kind = Kind::None;
    void* window = nullptr;
    void* display = nullptr;
};

/// A top-level window created by Platform::createWindow(). Destroy every window before its Platform.
/// Main thread only (asserted).
class Window {
public:
    virtual ~Window() = default;

    /// Size in screen coordinates.
    [[nodiscard]] virtual UVec2 windowSize() const = 0;
    /// Size of the drawable area in pixels; what rendering uses. 0 x 0 while minimized.
    [[nodiscard]] virtual UVec2 framebufferSize() const = 0;
    /// Ratio between pixels and screen coordinates for the window's monitor (1.0 at 96 DPI on Windows).
    [[nodiscard]] virtual Vec2 contentScale() const = 0;

    /// Resizes the window (screen coordinates). A WindowResized event follows.
    virtual void setSize(UVec2 size) = 0;
    /// Sets the title (UTF-8).
    virtual void setTitle(std::string_view title) = 0;

    /// True once the user asked to close the window; see WindowCloseRequested.
    [[nodiscard]] virtual bool shouldClose() const = 0;
    [[nodiscard]] virtual bool isFocused() const = 0;

    [[nodiscard]] virtual NativeWindowHandle nativeHandle() const = 0;

    /// The window's OpenGL context, or nullptr if it was created with GraphicsApi::None.
    [[nodiscard]] virtual OpenGlContext* openGlContext() = 0;

protected:
    Window() = default;
    Window(const Window&) = default;
    Window& operator=(const Window&) = default;
    Window(Window&&) = default;
    Window& operator=(Window&&) = default;
};

} // namespace teserra::platform
