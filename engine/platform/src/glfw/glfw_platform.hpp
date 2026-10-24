#pragma once

#include <teserra/core/types.hpp>
#include <teserra/platform/platform.hpp>

#include "common/main_thread.hpp"

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
    [[nodiscard]] const Clock& clock() const noexcept override { return m_clock; }
    [[nodiscard]] FileSystem& fileSystem() noexcept override { return m_fileSystem; }

    // For GlfwWindow.

    /// Queues an event from a GLFW callback; pollEvents() hands queued events to the caller's queue.
    void enqueue(const Event& event);
    void windowDestroyed() noexcept;
    [[nodiscard]] const MainThread& mainThread() const noexcept { return m_mainThread; }

private:
    MainThread m_mainThread;
    SteadyClock m_clock;
    NativeFileSystem m_fileSystem;
    std::vector<Event> m_pending; // events from GLFW callbacks since the last pollEvents()
    u32 m_windowCount = 0;
};

} // namespace teserra::platform
