#include "glfw/glfw_platform.hpp"

#include <teserra/core/assert.hpp>
#include <teserra/core/log.hpp>

#include "glfw/glfw_window.hpp"
#include <GLFW/glfw3.h>

#include <array>
#include <atomic>
#include <chrono>
#include <functional>
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

// GamepadButton and GamepadAxis follow GLFW's standard gamepad layout, so their values index GLFW's arrays.
static_assert(kMaxGamepads == GLFW_JOYSTICK_LAST + 1);
static_assert(kGamepadButtonCount == GLFW_GAMEPAD_BUTTON_LAST + 1);
static_assert(static_cast<int>(GamepadButton::South) == GLFW_GAMEPAD_BUTTON_A);
static_assert(static_cast<int>(GamepadButton::North) == GLFW_GAMEPAD_BUTTON_Y);
static_assert(static_cast<int>(GamepadButton::Guide) == GLFW_GAMEPAD_BUTTON_GUIDE);
static_assert(static_cast<int>(GamepadButton::LeftStick) == GLFW_GAMEPAD_BUTTON_LEFT_THUMB);
static_assert(static_cast<int>(GamepadButton::DpadUp) == GLFW_GAMEPAD_BUTTON_DPAD_UP);
static_assert(static_cast<int>(GamepadButton::DpadLeft) == GLFW_GAMEPAD_BUTTON_DPAD_LEFT);
static_assert(kGamepadAxisCount == GLFW_GAMEPAD_AXIS_LAST + 1);
static_assert(static_cast<int>(GamepadAxis::LeftY) == GLFW_GAMEPAD_AXIS_LEFT_Y);
static_assert(static_cast<int>(GamepadAxis::RightY) == GLFW_GAMEPAD_AXIS_RIGHT_Y);
static_assert(static_cast<int>(GamepadAxis::LeftTrigger) == GLFW_GAMEPAD_AXIS_LEFT_TRIGGER);

// The state of joystick `jid` in the engine's conventions: GLFW's stick Y is +1 down and its triggers rest
// at -1. Not connected if the joystick is absent or has no gamepad mapping.
GamepadSnapshot readGamepad(int jid) {
    GamepadSnapshot snapshot;
    GLFWgamepadstate state{};
    if (glfwJoystickIsGamepad(jid) != GLFW_TRUE || glfwGetGamepadState(jid, &state) != GLFW_TRUE) {
        return snapshot;
    }
    snapshot.connected = true;
    for (usize index = 0; index < kGamepadButtonCount; ++index) {
        snapshot.buttons[index] = state.buttons[index] == GLFW_PRESS;
    }
    for (usize index = 0; index < kGamepadAxisCount; ++index) {
        const f32 value = state.axes[index];
        switch (static_cast<GamepadAxis>(index)) {
            case GamepadAxis::LeftY:
            case GamepadAxis::RightY:
                snapshot.axes[index] = -value;
                break;
            case GamepadAxis::LeftTrigger:
            case GamepadAxis::RightTrigger:
                snapshot.axes[index] = (value + 1.0F) * 0.5F;
                break;
            case GamepadAxis::LeftX:
            case GamepadAxis::RightX:
                snapshot.axes[index] = value;
                break;
        }
    }
    return snapshot;
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
    // GLFW sets up joysticks on first use, which takes about 0.1 s on Windows (DirectInput): done here, it
    // does not stall the first frame's gamepad poll.
    static_cast<void>(glfwJoystickPresent(GLFW_JOYSTICK_1));
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
    m_polling = &queue;
    glfwPollEvents();
    m_polling = nullptr;
    handOver(queue);
    pollGamepads(queue);
}

void GlfwPlatform::waitEvents(EventQueue& queue, std::chrono::nanoseconds timeout) {
    TESERRA_ASSERT(m_mainThread.isCurrent(), "Events must be polled on the main thread");
    const double seconds = std::chrono::duration<double>(timeout).count();
    if (seconds > 0.0) {
        m_polling = &queue;
        glfwWaitEventsTimeout(seconds);
        m_polling = nullptr;
    }
    pollEvents(queue); // hands over what the wait received, and anything that came right after
}

void GlfwPlatform::enqueue(const Event& event) {
    m_pending.push_back(event);
}

void GlfwPlatform::framebufferResized(UVec2 framebufferSize) {
    // Inside a poll, a resize comes from the window system: on Windows from the loop it runs while a
    // window border is dragged, which only returns when the mouse button is released.
    const std::function<void()>& handler = liveResizeHandler();
    if (m_polling == nullptr || m_inLiveResize || !handler || framebufferSize.x == 0 ||
        framebufferSize.y == 0) {
        return;
    }
    handOver(*m_polling);
    m_inLiveResize = true;
    handler();
    m_inLiveResize = false;
}

void GlfwPlatform::handOver(EventQueue& queue) {
    for (const Event& event : m_pending) {
        queue.push(event);
    }
    m_pending.clear();
}

void GlfwPlatform::pollGamepads(EventQueue& queue) {
    for (u32 index = 0; index < kMaxGamepads; ++index) {
        const int jid = GLFW_JOYSTICK_1 + static_cast<int>(index);
        const GamepadSnapshot snapshot = readGamepad(jid);
        if (snapshot.connected != m_gamepads.wasConnected(index)) {
            if (snapshot.connected) {
                const char* name = glfwGetGamepadName(jid);
                TESERRA_LOG_INFO(LogCategory::Platform, "Gamepad {} connected: {}", index,
                                 name != nullptr ? name : "unknown");
            } else {
                TESERRA_LOG_INFO(LogCategory::Platform, "Gamepad {} disconnected", index);
            }
        }
        m_gamepads.update(index, snapshot, queue);
    }
}

void GlfwPlatform::windowDestroyed() noexcept {
    --m_windowCount;
}

} // namespace teserra::platform
