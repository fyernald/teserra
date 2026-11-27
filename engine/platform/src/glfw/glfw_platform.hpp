#pragma once

#include <teserra/core/types.hpp>
#include <teserra/platform/platform.hpp>

#include "common/gamepad_tracker.hpp"
#include "common/main_thread.hpp"

#include <chrono>
#include <vector>

namespace teserra::platform {

/// Platform backed by GLFW. Platform::create() initializes GLFW; the destructor terminates it.
class GlfwPlatform final : public Platform {
public:
    /// Use Platform::create(), which initializes GLFW and enforces a single platform.
    GlfwPlatform() = default;
    ~GlfwPlatform() override;

    [[nodiscard]] Result<std::unique_ptr<Window>> createWindow(const WindowDesc& desc) override;
    void pollEvents(EventQueue& queue) override;
    void waitEvents(EventQueue& queue, std::chrono::nanoseconds timeout) override;
    [[nodiscard]] const Clock& clock() const noexcept override { return m_clock; }
    [[nodiscard]] FileSystem& fileSystem() noexcept override { return m_fileSystem; }

    // For GlfwWindow.

    /// Queues an event from a GLFW callback; pollEvents() hands queued events to the caller's queue.
    void enqueue(const Event& event);
    /// After enqueuing a WindowResized: inside a poll, hands the events so far to the poll's queue and runs
    /// the live-resize handler (Platform::setLiveResizeHandler()).
    void framebufferResized(UVec2 framebufferSize);
    void windowDestroyed() noexcept;
    [[nodiscard]] const MainThread& mainThread() const noexcept { return m_mainThread; }

private:
    // Moves the pending events to `queue`.
    void handOver(EventQueue& queue);
    // Reads every gamepad and appends what changed since the last poll to `queue`.
    void pollGamepads(EventQueue& queue);

    MainThread m_mainThread;
    SteadyClock m_clock;
    NativeFileSystem m_fileSystem;
    std::vector<Event> m_pending; // events from GLFW callbacks since the last pollEvents()
    GamepadTracker m_gamepads;
    EventQueue* m_polling = nullptr; // the queue of the poll underway, if any
    bool m_inLiveResize = false;
    u32 m_windowCount = 0;
};

} // namespace teserra::platform
