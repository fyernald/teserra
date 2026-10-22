#include "glfw/glfw_platform.hpp"

#include <teserra/core/assert.hpp>
#include <teserra/core/log.hpp>

#include "glfw/glfw_window.hpp"
#include <GLFW/glfw3.h>

#include <atomic>
#include <string_view>

namespace teserra::platform {

namespace {

// GLFW is global state, so only one platform may own it at a time.
std::atomic<bool> platformExists{false};

// GLFW reports errors both through this callback and through glfwGetError(). Failures are returned as
// Error values (and logged by whoever handles them), so the callback only adds diagnostics.
void onGlfwError(int code, const char* description) {
    TESERRA_LOG_DEBUG(LogCategory::Platform, "GLFW error 0x{:X}: {}", code, description);
}

std::string_view lastGlfwError() noexcept {
    const char* description = nullptr;
    glfwGetError(&description);
    return description != nullptr ? description : "unknown GLFW error";
}

int glfwBool(bool value) noexcept {
    return value ? GLFW_TRUE : GLFW_FALSE;
}

} // namespace

Result<std::unique_ptr<Platform>> Platform::create(const PlatformDesc& /*desc*/) {
    if (platformExists.exchange(true)) {
        return fail(ErrorCode::AlreadyExists, Module::Platform,
                    "A Platform exists already; destroy it first");
    }
    glfwSetErrorCallback(&onGlfwError);
    if (glfwInit() != GLFW_TRUE) {
        platformExists = false;
        return fail(ErrorCode::DeviceFailure, Module::Platform, "Cannot initialize GLFW: {}",
                    lastGlfwError());
    }
    TESERRA_LOG_INFO(LogCategory::Platform, "Window system: GLFW {}", glfwGetVersionString());
    return std::make_unique<GlfwPlatform>();
}

GlfwPlatform::~GlfwPlatform() {
    TESERRA_ASSERT(m_mainThread.isCurrent(), "The Platform must be destroyed on the main thread");
    TESERRA_ASSERT(m_windowCount == 0, "Destroy all {} windows before the Platform", m_windowCount);
    glfwTerminate();
    platformExists = false;
}

Result<std::unique_ptr<Window>> GlfwPlatform::createWindow(const WindowDesc& desc) {
    TESERRA_ASSERT(m_mainThread.isCurrent(), "Windows must be created on the main thread");
    TESERRA_ASSERT(desc.size.x > 0 && desc.size.y > 0, "Window size {}x{} is empty", desc.size.x,
                   desc.size.y);

    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_RESIZABLE, glfwBool(desc.resizable));
    glfwWindowHint(GLFW_VISIBLE, glfwBool(desc.visible));
    // Window sizes are scaled by the monitor's content scale, so a 1280x720 window looks the same on a
    // 150% display as on a 100% one.
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);

    const bool openGl = desc.graphicsApi == GraphicsApi::OpenGL;
    if (openGl) {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, glfwBool(desc.debugContext));
    } else {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    }

    GLFWwindow* window = glfwCreateWindow(static_cast<int>(desc.size.x), static_cast<int>(desc.size.y),
                                          desc.title.c_str(), nullptr, nullptr);
    if (window == nullptr) {
        if (openGl) {
            return fail(ErrorCode::DeviceFailure, Module::Platform,
                        "Cannot create window '{}' with an OpenGL 4.5 core context: {}", desc.title,
                        lastGlfwError());
        }
        return fail(ErrorCode::DeviceFailure, Module::Platform, "Cannot create window '{}': {}", desc.title,
                    lastGlfwError());
    }

    ++m_windowCount;
    auto result = std::make_unique<GlfwWindow>(*this, window, openGl);
    if (openGl) {
        result->makeCurrent();
    }
    return result;
}

void GlfwPlatform::pollEvents(EventQueue& queue) {
    TESERRA_ASSERT(m_mainThread.isCurrent(), "Events must be polled on the main thread");
    glfwPollEvents();
    for (const Event& event : m_pending) {
        queue.push(event);
    }
    m_pending.clear();
}

void GlfwPlatform::enqueue(const Event& event) {
    m_pending.push_back(event);
}

void GlfwPlatform::windowDestroyed() noexcept {
    --m_windowCount;
}

} // namespace teserra::platform
