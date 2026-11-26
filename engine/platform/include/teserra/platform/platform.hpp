#pragma once

// Platform layer entry point (docs/architecture/platform-abstraction.md, ADR-0006, ADR-0015).

#include <teserra/core/result.hpp>
#include <teserra/platform/clock.hpp>
#include <teserra/platform/event.hpp>
#include <teserra/platform/file_system.hpp>
#include <teserra/platform/window.hpp>

#include <chrono>
#include <functional>
#include <memory>
#include <utility>

namespace teserra::platform {

/// Creation parameters of the platform. Empty for now.
struct PlatformDesc {};

/// Abstract factory for the window system and OS services. Owns the window system's global state (it
/// initializes and shuts down GLFW), so at most one Platform exists at a time. Created once by the
/// application or a test and passed by reference: not a singleton. The thread that creates it is the main
/// thread; every function must be called there (asserted).
class Platform {
public:
    /// Creates the platform of the window backend selected in CMake (TESERRA_WINDOW_BACKEND). Fails with
    /// ErrorCode::AlreadyExists while another Platform exists, and with ErrorCode::DeviceFailure if the
    /// window system cannot be initialized (e.g. no desktop session).
    [[nodiscard]] static Result<std::unique_ptr<Platform>> create(const PlatformDesc& desc = {});

    /// Destroying the platform shuts the window system down; every window must be destroyed first
    /// (asserted).
    virtual ~Platform() = default;

    /// Creates a window. With GraphicsApi::OpenGL it fails with ErrorCode::DeviceFailure if no OpenGL 4.5
    /// core context can be created, naming the reason.
    [[nodiscard]] virtual Result<std::unique_ptr<Window>> createWindow(const WindowDesc& desc) = 0;

    /// Processes pending window system messages and appends the resulting events to `queue` (which is not
    /// cleared first). Call once per frame.
    virtual void pollEvents(EventQueue& queue) = 0;

    /// Like pollEvents(), but first sleeps until a window system message arrives or `timeout` has passed,
    /// so a loop with nothing to draw (a minimized window) does not spin. The default implementation does
    /// not wait; platforms with a window system override it.
    virtual void waitEvents(EventQueue& queue, std::chrono::nanoseconds timeout) {
        static_cast<void>(timeout);
        pollEvents(queue);
    }

    /// Sets the function that draws frames while the window system resizes the window inside a poll.
    ///
    /// On Windows, dragging a window border keeps the main thread in the window system's own loop until
    /// the mouse button is released: pollEvents() does not return, and without a handler nothing draws the
    /// window at its new sizes, so it shows its last frame cut off or stretched (#184). A platform with
    /// such a loop calls `handler` from inside pollEvents() or waitEvents() each time the framebuffer gets
    /// a new size there, other than 0 (minimized). By then the events of the poll so far, ending with that
    /// WindowResized, are in the queue passed to the poll; the handler finishes a frame with them and may
    /// clear the queue, which the poll goes on appending later events to. It is not called while it runs,
    /// nor outside a poll (Window::setSize() resizes without it). An empty function removes it. Main
    /// thread only.
    void setLiveResizeHandler(std::function<void()> handler) { m_liveResizeHandler = std::move(handler); }

    /// The monotonic clock that drives the main loop: real time (SteadyClock) here, a ManualClock in a
    /// test platform. Thread-safe, unlike the rest of the platform.
    [[nodiscard]] virtual const Clock& clock() const noexcept = 0;

    /// The file system: the operating system's (NativeFileSystem) here, possibly another one in a test
    /// platform. Like clock(), it may be used from any thread.
    [[nodiscard]] virtual FileSystem& fileSystem() noexcept = 0;

    Platform(const Platform&) = delete;
    Platform& operator=(const Platform&) = delete;
    Platform(Platform&&) = delete;
    Platform& operator=(Platform&&) = delete;

protected:
    Platform() = default;

    /// The function set with setLiveResizeHandler(); empty without one.
    [[nodiscard]] const std::function<void()>& liveResizeHandler() const noexcept {
        return m_liveResizeHandler;
    }

private:
    std::function<void()> m_liveResizeHandler;
};

} // namespace teserra::platform
