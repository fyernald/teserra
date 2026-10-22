#pragma once

// The application object: owns the engine services and runs the main loop (docs/architecture/
// public-api.md#application-lifecycle, docs/architecture/overview.md#frame-flow-v1).

#include <teserra/core/math.hpp>
#include <teserra/core/result.hpp>
#include <teserra/core/types.hpp>
#include <teserra/platform/event.hpp>
#include <teserra/platform/fixed_step_accumulator.hpp>
#include <teserra/platform/frame_timer.hpp>
#include <teserra/platform/input.hpp>
#include <teserra/platform/platform.hpp>
#include <teserra/platform/window.hpp>

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace teserra::engine {

/// The program's arguments, as main() receives them: `.args = {argc, argv}`.
struct CommandLine {
    int argc = 0;
    const char* const* argv = nullptr;
};

/// Settings of an App. The engine flags in `args` override the matching fields:
///   --frames N        maxFrames = N (exit after N frames, e.g. for smoke runs)
///   --hidden-window   windowVisible = false
/// Other arguments are left to the game.
struct AppConfig {
    std::string title = "Teserra";
    /// Window size in screen coordinates.
    UVec2 windowSize{1280, 720};
    bool resizable = true;
    bool windowVisible = true;
    /// Wait for the display's vertical blank when presenting. Paces the loop when the window has an
    /// OpenGL context.
    bool vsync = true;
    /// OpenGL until the RHI chooses the backend (iteration 4); None for windows that are never presented.
    platform::GraphicsApi graphicsApi = platform::GraphicsApi::OpenGL;
    /// Length of one fixedUpdate step.
    std::chrono::nanoseconds fixedStep = platform::FixedStepAccumulator::kDefaultStep;
    /// Fixed steps run at most per frame; the rest are dropped after a stall.
    u32 maxFixedStepsPerFrame = platform::FixedStepAccumulator::kDefaultMaxStepsPerFrame;
    /// Number of frames after which run() returns; 0 runs until quit() or the window closes.
    u64 maxFrames = 0;
    CommandLine args;
};

class App;

/// What the game does each frame, until scenes and systems take over (iteration 10). Every callback is
/// optional.
struct AppCallbacks {
    /// Called for every event of the frame, after App::input() has processed it.
    std::function<void(App& app, const platform::Event& event)> onEvent;
    /// Called zero or more times per frame, once per fixed step of `stepSeconds`.
    std::function<void(App& app, f32 stepSeconds)> fixedUpdate;
    /// Called once per frame with the frame's real time.
    std::function<void(App& app, f32 deltaSeconds)> update;
};

/// Owns the platform, the window and the per-frame services, and runs the main loop. Not a singleton:
/// several Apps can exist (with platforms that allow it, such as a test platform). Created, run and
/// destroyed on the main thread.
///
///     int main(int argc, char** argv) {
///         engine::AppConfig config;
///         config.title = "Game";
///         config.args = {argc, argv};
///         engine::App app{config};
///
///         engine::AppCallbacks callbacks;
///         callbacks.update = [](engine::App& app, f32 deltaSeconds) { ... };
///         return app.run(callbacks);
///     }
///
/// Construction creates the services in dependency order; destruction releases them in reverse. A
/// failure is logged and stored (initError()), and run() then returns 1: the engine does not throw.
class App {
public:
    /// Creates the platform of the window backend selected in CMake.
    explicit App(AppConfig config);
    /// Uses `platform`, e.g. a test platform with a ManualClock. If `platform` is null, initialization
    /// fails.
    App(AppConfig config, std::unique_ptr<platform::Platform> platform);
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;
    App(App&&) = delete;
    App& operator=(App&&) = delete;

    /// Runs frames until quit(), a close request or maxFrames:
    /// events -> fixedUpdate x N -> update -> present. Returns the exit code given to quit() (0 by
    /// default), or 1 at once if initialization failed.
    [[nodiscard]] int run(const AppCallbacks& callbacks = {});

    /// Ends run() after the current frame, which returns `exitCode`; the last call wins. A close
    /// request or reaching maxFrames calls quit(0) unless the game already quit.
    void quit(int exitCode = 0) noexcept;
    [[nodiscard]] bool isQuitting() const noexcept { return m_quitting; }

    /// Frames run() has completed so far.
    [[nodiscard]] u64 frameCount() const noexcept { return m_frameCount; }

    [[nodiscard]] bool isInitialized() const noexcept { return !m_initError.has_value(); }
    /// Why initialization failed, or nullptr.
    [[nodiscard]] const Error* initError() const noexcept { return m_initError ? &*m_initError : nullptr; }

    /// The configuration after the command line was applied.
    [[nodiscard]] const AppConfig& config() const noexcept { return m_config; }

    // Services; initialization must have succeeded (asserted).
    [[nodiscard]] platform::Platform& platform();
    [[nodiscard]] platform::Window& window();
    [[nodiscard]] const platform::Input& input() const noexcept { return m_input; }
    [[nodiscard]] const platform::FrameTimer& frameTimer() const noexcept { return m_frameTimer; }
    [[nodiscard]] const platform::FixedStepAccumulator& fixedStep() const noexcept { return m_fixedStep; }

private:
    void start(Result<std::unique_ptr<platform::Platform>> platform);
    Result<void> initialize(Result<std::unique_ptr<platform::Platform>> platform);
    void runFrame(const AppCallbacks& callbacks);
    void present();

    AppConfig m_config;
    std::optional<Error> m_initError;
    // Services in dependency order: members are destroyed in reverse, the window before the platform.
    std::unique_ptr<platform::Platform> m_platform;
    std::unique_ptr<platform::Window> m_window;
    platform::Input m_input;
    platform::EventQueue m_events;
    platform::FrameTimer m_frameTimer;
    platform::FixedStepAccumulator m_fixedStep;
    u64 m_frameCount = 0;
    bool m_quitting = false;
    int m_exitCode = 0;
};

} // namespace teserra::engine
