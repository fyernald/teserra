#pragma once

#include <teserra/platform/opengl_context.hpp>
#include <teserra/platform/window.hpp>

struct GLFWwindow;

namespace teserra::platform {

class GlfwPlatform;

/// Window backed by GLFW. Implements OpenGlContext too, but only exposes it when created with
/// GraphicsApi::OpenGL.
class GlfwWindow final : public Window, public OpenGlContext {
public:
    /// Takes ownership of `window`, a window GLFW just created, and starts forwarding its events.
    GlfwWindow(GlfwPlatform& platform, GLFWwindow* window, bool hasOpenGlContext);
    ~GlfwWindow() override;

    GlfwWindow(const GlfwWindow&) = delete;
    GlfwWindow& operator=(const GlfwWindow&) = delete;
    GlfwWindow(GlfwWindow&&) = delete;
    GlfwWindow& operator=(GlfwWindow&&) = delete;

    // Window
    [[nodiscard]] UVec2 windowSize() const override;
    [[nodiscard]] UVec2 framebufferSize() const override;
    [[nodiscard]] Vec2 contentScale() const override;
    void setSize(UVec2 size) override;
    void setTitle(std::string_view title) override;
    [[nodiscard]] bool shouldClose() const override;
    [[nodiscard]] bool isFocused() const override;
    [[nodiscard]] NativeWindowHandle nativeHandle() const override;
    [[nodiscard]] OpenGlContext* openGlContext() override;

    // OpenGlContext
    void makeCurrent() override;
    void swapBuffers() override;
    void setSwapInterval(int interval) override;
    [[nodiscard]] GlProc getProcAddress(const char* name) override;

private:
    static void onFramebufferSize(GLFWwindow* window, int width, int height);
    static void onClose(GLFWwindow* window);
    static void onFocus(GLFWwindow* window, int focused);
    static void onKey(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void onChar(GLFWwindow* window, unsigned int codepoint);
    static void onMouseButton(GLFWwindow* window, int button, int action, int mods);
    static void onCursorPos(GLFWwindow* window, double x, double y);
    static void onScroll(GLFWwindow* window, double x, double y);

    void assertMainThread() const;

    GlfwPlatform& m_platform;
    GLFWwindow* m_window;
    bool m_hasOpenGlContext;
};

} // namespace teserra::platform
