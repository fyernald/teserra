#pragma once

// Platform layer entry point (docs/architecture/platform-abstraction.md, ADR-0006, ADR-0015).

#include <teserra/core/result.hpp>
#include <teserra/platform/clock.hpp>
#include <teserra/platform/event.hpp>
#include <teserra/platform/window.hpp>

#include <memory>

namespace teserra::platform {

/// Creation parameters of the platform. Empty for now; OS services (file system) add settings.
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

    /// The monotonic clock that drives the main loop: real time (SteadyClock) here, a ManualClock in a
    /// test platform. Thread-safe, unlike the rest of the platform.
    [[nodiscard]] virtual const Clock& clock() const noexcept = 0;

    Platform(const Platform&) = delete;
    Platform& operator=(const Platform&) = delete;
    Platform(Platform&&) = delete;
    Platform& operator=(Platform&&) = delete;

protected:
    Platform() = default;
};

} // namespace teserra::platform
