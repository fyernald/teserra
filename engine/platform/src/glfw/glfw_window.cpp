#include "glfw/glfw_window.hpp"

#include <teserra/core/assert.hpp>

#include "glfw/glfw_key_table.hpp"
#include "glfw/glfw_native.hpp"
#include "glfw/glfw_platform.hpp"
#include <GLFW/glfw3.h>

#include <optional>
#include <string>

namespace teserra::platform {

namespace {

GlfwWindow& windowOf(GLFWwindow* window) noexcept {
    return *static_cast<GlfwWindow*>(glfwGetWindowUserPointer(window));
}

UVec2 toSize(int width, int height) noexcept {
    return {static_cast<u32>(width > 0 ? width : 0), static_cast<u32>(height > 0 ? height : 0)};
}

} // namespace

GlfwWindow::GlfwWindow(GlfwPlatform& platform, GLFWwindow* window, bool hasOpenGlContext)
    : m_platform(platform), m_window(window), m_hasOpenGlContext(hasOpenGlContext) {
    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, &GlfwWindow::onFramebufferSize);
    glfwSetWindowCloseCallback(m_window, &GlfwWindow::onClose);
    glfwSetWindowFocusCallback(m_window, &GlfwWindow::onFocus);
    glfwSetKeyCallback(m_window, &GlfwWindow::onKey);
    glfwSetCharCallback(m_window, &GlfwWindow::onChar);
    glfwSetMouseButtonCallback(m_window, &GlfwWindow::onMouseButton);
    glfwSetCursorPosCallback(m_window, &GlfwWindow::onCursorPos);
    glfwSetScrollCallback(m_window, &GlfwWindow::onScroll);
}

GlfwWindow::~GlfwWindow() {
    assertMainThread();
    glfwDestroyWindow(m_window);
    m_platform.windowDestroyed();
}

UVec2 GlfwWindow::windowSize() const {
    assertMainThread();
    int width = 0;
    int height = 0;
    glfwGetWindowSize(m_window, &width, &height);
    return toSize(width, height);
}

UVec2 GlfwWindow::framebufferSize() const {
    assertMainThread();
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(m_window, &width, &height);
    return toSize(width, height);
}

Vec2 GlfwWindow::contentScale() const {
    assertMainThread();
    Vec2 scale{1.0F};
    glfwGetWindowContentScale(m_window, &scale.x, &scale.y);
    return scale;
}

void GlfwWindow::setSize(UVec2 size) {
    assertMainThread();
    TESERRA_ASSERT(size.x > 0 && size.y > 0, "Window size {}x{} is empty", size.x, size.y);
    glfwSetWindowSize(m_window, static_cast<int>(size.x), static_cast<int>(size.y));
}

void GlfwWindow::setTitle(std::string_view title) {
    assertMainThread();
    const std::string terminated(title); // GLFW needs a null-terminated string
    glfwSetWindowTitle(m_window, terminated.c_str());
}

bool GlfwWindow::shouldClose() const {
    assertMainThread();
    return glfwWindowShouldClose(m_window) == GLFW_TRUE;
}

bool GlfwWindow::isFocused() const {
    assertMainThread();
    return glfwGetWindowAttrib(m_window, GLFW_FOCUSED) == GLFW_TRUE;
}

NativeWindowHandle GlfwWindow::nativeHandle() const {
    assertMainThread();
    return nativeHandleOf(m_window);
}

OpenGlContext* GlfwWindow::openGlContext() {
    assertMainThread();
    return m_hasOpenGlContext ? this : nullptr;
}

void GlfwWindow::makeCurrent() {
    assertMainThread();
    glfwMakeContextCurrent(m_window);
}

void GlfwWindow::swapBuffers() {
    assertMainThread();
    glfwSwapBuffers(m_window);
}

void GlfwWindow::setSwapInterval(int interval) {
    assertMainThread();
    TESERRA_ASSERT(glfwGetCurrentContext() == m_window,
                   "setSwapInterval() needs this window's context current");
    glfwSwapInterval(interval);
}

GlProc GlfwWindow::getProcAddress(const char* name) {
    TESERRA_ASSERT(glfwGetCurrentContext() == m_window,
                   "getProcAddress() needs this window's context current");
    return glfwGetProcAddress(name);
}

void GlfwWindow::onFramebufferSize(GLFWwindow* window, int width, int height) {
    int windowWidth = 0;
    int windowHeight = 0;
    glfwGetWindowSize(window, &windowWidth, &windowHeight);
    windowOf(window).m_platform.enqueue(
        WindowResized{toSize(windowWidth, windowHeight), toSize(width, height)});
}

void GlfwWindow::onClose(GLFWwindow* window) {
    windowOf(window).m_platform.enqueue(WindowCloseRequested{});
}

void GlfwWindow::onFocus(GLFWwindow* window, int focused) {
    windowOf(window).m_platform.enqueue(FocusChanged{focused == GLFW_TRUE});
}

void GlfwWindow::onKey(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/) {
    const Key engineKey = keyFromGlfw(key);
    if (engineKey == Key::Unknown) {
        return;
    }
    windowOf(window).m_platform.enqueue(
        KeyChanged{.key = engineKey, .down = action != GLFW_RELEASE, .repeat = action == GLFW_REPEAT});
}

void GlfwWindow::onChar(GLFWwindow* window, unsigned int codepoint) {
    windowOf(window).m_platform.enqueue(TextEntered{static_cast<char32_t>(codepoint)});
}

void GlfwWindow::onMouseButton(GLFWwindow* window, int button, int action, int /*mods*/) {
    if (const std::optional<MouseButton> engineButton = mouseButtonFromGlfw(button)) {
        windowOf(window).m_platform.enqueue(MouseButtonChanged{*engineButton, action == GLFW_PRESS});
    }
}

void GlfwWindow::onCursorPos(GLFWwindow* window, double x, double y) {
    windowOf(window).m_platform.enqueue(MouseMoved{Vec2{static_cast<f32>(x), static_cast<f32>(y)}});
}

void GlfwWindow::onScroll(GLFWwindow* window, double x, double y) {
    windowOf(window).m_platform.enqueue(MouseScrolled{Vec2{static_cast<f32>(x), static_cast<f32>(y)}});
}

void GlfwWindow::assertMainThread() const {
    TESERRA_ASSERT(m_platform.mainThread().isCurrent(), "Window functions must be called on the main thread");
}

} // namespace teserra::platform
