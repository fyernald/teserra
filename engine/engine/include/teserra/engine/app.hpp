#pragma once

// The application object: owns the engine services and runs the main loop (docs/architecture/
// public-api.md#application-lifecycle, docs/architecture/overview.md#frame-flow-v1).

#include <teserra/assets/asset_registry.hpp>
#include <teserra/assets/mount_table.hpp>
#include <teserra/core/math.hpp>
#include <teserra/core/result.hpp>
#include <teserra/core/types.hpp>
#include <teserra/platform/event.hpp>
#include <teserra/platform/fixed_step_accumulator.hpp>
#include <teserra/platform/frame_timer.hpp>
#include <teserra/platform/input.hpp>
#include <teserra/platform/key.hpp>
#include <teserra/platform/platform.hpp>
#include <teserra/platform/window.hpp>
#include <teserra/renderer/chunk_mesh_cache.hpp>
#include <teserra/renderer/renderer.hpp>
#include <teserra/renderer/sprite_pass.hpp>
#include <teserra/rhi/command_list.hpp>
#include <teserra/rhi/device.hpp>
#include <teserra/rhi/handles.hpp>
#include <teserra/scene/scene.hpp>
#include <teserra/scene/scene_manager.hpp>
#if TESERRA_WITH_DEBUG_UI
#include <teserra/debug_ui/debug_ui.hpp>
#include <teserra/debug_ui/log_console.hpp>
#endif

#include <array>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#if TESERRA_WITH_DEBUG_UI
namespace teserra::debugui {
class ImGuiPass;
} // namespace teserra::debugui
#endif

namespace teserra::engine {

/// The program's arguments, as main() receives them: `.args = {argc, argv}`.
struct CommandLine {
    int argc = 0;
    const char* const* argv = nullptr;
};

/// The in-game debug UI (App::debugUi()): Dear ImGui windows over the game, such as the frame stats, opened
/// and closed with a key. Builds without TESERRA_WITH_DEBUG_UI ignore it, so a game's AppConfig builds
/// either way.
struct DebugUiSettings {
    /// false: no debug UI, and the toggle key goes to the game like any other.
    bool enabled = true;
    /// Opens and closes the debug UI; the game never sees it, nor the text it types. Key::Unknown leaves
    /// only App::setDebugUiOpen().
    platform::Key toggleKey = platform::Key::GraveAccent;
    /// Whether the debug UI is open from the first frame.
    bool startOpen = false;
};

/// A mount point of App::mounts(): virtual paths "<scheme>://..." refer to files under `directory`.
struct AssetMount {
    std::string scheme;
    /// A relative directory is relative to the executable's directory, not to the working directory.
    std::string directory;
};

/// Settings of an App. The engine flags in `args` override the matching fields:
///   --frames N        maxFrames = N (exit after N frames, e.g. for smoke runs)
///   --hidden-window   windowVisible = false
///   --debug-ui        debugUi.startOpen = true
/// Other arguments are left to the game.
struct AppConfig {
    std::string title = "Teserra";
    /// Window size in screen coordinates.
    UVec2 windowSize{1280, 720};
    bool resizable = true;
    bool windowVisible = true;
    /// Wait for the display's vertical blank when presenting (rhi::PresentMode::Fifo), which paces the
    /// loop; false presents at once (Immediate).
    bool vsync = true;
    /// The GPU backend of the App's device. It must be compiled in (TESERRA_RHI_OPENGL, ...), or
    /// initialization fails. std::nullopt runs without a device: the window gets no graphics context and
    /// nothing is rendered or presented, e.g. for tests and tools.
    std::optional<rhi::Backend> backend = rhi::Backend::OpenGL;
    /// The size of the scene renderer's main target and how it is presented (renderer.md#render-targets):
    /// e.g. `.sceneTarget = {.resolution = {320, 180}}` renders pixel art at 320x180 and scales it by whole
    /// factors to the window, letterboxed. The default matches the window.
    renderer::SceneTargetConfig sceneTarget{};
    /// The scene renderer's sprite pass: pixel art wants `.filter = rhi::Filter::Nearest`.
    renderer::SpritePassConfig spritePass{};
    /// Length of one fixedUpdate step.
    std::chrono::nanoseconds fixedStep = platform::FixedStepAccumulator::kDefaultStep;
    /// Fixed steps run at most per frame; the rest are dropped after a stall.
    u32 maxFixedStepsPerFrame = platform::FixedStepAccumulator::kDefaultMaxStepsPerFrame;
    /// Number of frames after which run() returns; 0 runs until quit() or the window closes.
    u64 maxFrames = 0;
    /// While the window has no image to render into (minimized), the App is paused: each frame waits up to
    /// this long for window events instead of spinning, handles them (closing and quit() still work), and
    /// runs nothing else: no fixed steps, updates, scene phases or rendering, and scene Time stands still.
    /// The frame after it gets an image back resumes with at most this much time to catch up.
    std::chrono::nanoseconds minimizedEventWait = std::chrono::milliseconds{100};
    DebugUiSettings debugUi{};
    /// Mount points of App::mounts() (docs/architecture/assets.md#mount-points-and-files). Unless listed
    /// here, "assets" is mounted to `<executable dir>/assets` and "engine" to `<executable dir>/engine`;
    /// those defaults may be missing. Initialization fails if a listed directory does not exist, or a scheme
    /// is invalid or listed twice.
    std::vector<AssetMount> mounts{};
    CommandLine args{};
};

class App;

/// What the game does each frame outside a scene's systems, e.g. in examples without scenes. Every callback
/// is optional.
struct AppCallbacks {
    /// Called for every event of the frame, after App::input() has processed it.
    std::function<void(App& app, const platform::Event& event)> onEvent{};
    /// Called zero or more times per frame, once per fixed step of `stepSeconds`.
    std::function<void(App& app, f32 stepSeconds)> fixedUpdate{};
    /// Called once per frame with the frame's real time.
    std::function<void(App& app, f32 deltaSeconds)> update{};
    /// Records the frame's GPU work, after update(); the App submits it and presents App::backbuffer().
    /// Not called while the window has no image (minimized). Without it, the App renders the active scene
    /// (what its Extract phase put in its scene::RenderExtraction), or clears the image to black without one.
    std::function<void(App& app, rhi::CommandList& commands)> render{};
};

/// Owns the platform, the window and the per-frame services, and runs the main loop. Not a singleton:
/// several Apps can exist (with platforms that allow it, such as a test platform). Created, run and
/// destroyed on the main thread.
///
///     int main(int argc, char** argv) {
///         engine::App app{{.title = "Game", .args = {argc, argv}}};
///         return app.run({.update = [](engine::App& app, f32 deltaSeconds) { ... }});
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
    /// events -> fixedUpdate x N -> update -> render -> submit and present. With the debug UI open, its
    /// frame starts after the events and its windows are drawn over the frame's image after render. While the
    /// window is minimized, frames only wait for and handle events (AppConfig::minimizedEventWait). Returns
    /// the exit code given to quit() (0 by default), or 1 at once if initialization failed.
    [[nodiscard]] int run(const AppCallbacks& callbacks = {});

    /// Runs frames like run(callbacks), with `scene` active from the first frame. `scene` must come from
    /// scenes() (asserted). Each frame runs the active scene's phases, with the callbacks next to them:
    /// frame boundary (scene switch; Startup the first time a scene runs) -> events -> PreUpdate ->
    /// FixedUpdate and fixedUpdate x N -> Update -> update -> PostUpdate -> Extract -> render -> Last.
    /// The active scene's Time resource advances every frame before PreUpdate, and its platform::Input
    /// resource (inserted the first frame) is set to input() after the events. Before Extract, its
    /// scene::RenderExtraction gets the size of the scene target (the swapchain's with a render callback) and
    /// the renderer; without a render callback, the App renders what extraction produced with renderer().
    /// Switches requested with scenes().switchTo() take effect at the next frame boundary.
    [[nodiscard]] int run(scene::Scene& scene, const AppCallbacks& callbacks = {});

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
    /// The platform: clock, file system, window system.
    [[nodiscard]] platform::Platform& platform();
    [[nodiscard]] platform::Window& window();
    /// The mount points that asset paths ("assets://...") resolve through.
    [[nodiscard]] assets::MountTable& mounts();
    /// The assets, read through mounts(). Loaders: assets::ImageLoader (ImageData), and with a device the
    /// renderer's (renderer::Texture, renderer::Shader). Assets nothing refers to are unloaded at the end of
    /// each frame. Every Asset<T> must be gone before the App is destroyed (asserted).
    [[nodiscard]] assets::AssetRegistry& assets();
    /// The GPU device; AppConfig::backend must not be std::nullopt (asserted).
    [[nodiscard]] rhi::Device& device();
    /// The renderer that draws scenes when there is no render callback, created on first use with
    /// AppConfig::sceneTarget, its sprite pass and its debug pass; nullptr if it could not be created
    /// (logged, and run() then ends with 1). Games create the render targets of cameras with it. Needs a
    /// device (asserted).
    [[nodiscard]] renderer::Renderer* renderer();
    /// The swapchain image of the current frame, to render into during AppCallbacks::render; a null handle
    /// outside it.
    [[nodiscard]] rhi::TextureHandle backbuffer() const noexcept { return m_backbuffer; }
    [[nodiscard]] const platform::Input& input() const noexcept { return m_input; }
    [[nodiscard]] const platform::FrameTimer& frameTimer() const noexcept { return m_frameTimer; }
    [[nodiscard]] const platform::FixedStepAccumulator& fixedStep() const noexcept { return m_fixedStep; }
    /// Whether the last frame was paused because the window had no image (minimized).
    [[nodiscard]] bool isPaused() const noexcept { return m_paused; }

    /// The scenes of this App and the active one. Available before initialization too, so that a game can
    /// build its scenes first.
    [[nodiscard]] scene::SceneManager& scenes() noexcept { return m_scenes; }
    [[nodiscard]] const scene::SceneManager& scenes() const noexcept { return m_scenes; }

#if TESERRA_WITH_DEBUG_UI
    /// The debug UI; AppConfig::debugUi.enabled must be true (asserted). Games add windows of their own with
    /// addWindow(); the App adds "Stats" (frame times, fixed steps, the active scene, sizes). While it is
    /// open, ImGui:: calls work in the frame's events, the active scene's phases from PreUpdate to Extract,
    /// and the callbacks (not render); its windows are drawn after them. While it is closed, frames run no
    /// ImGui at all. Mouse buttons and wheel over its windows, and keys and text while one of its text fields
    /// has focus, do not reach input() or AppCallbacks::onEvent (releases always do).
    [[nodiscard]] debugui::DebugUi& debugUi();
    [[nodiscard]] bool isDebugUiOpen() const noexcept { return m_debugUiOpen; }
    /// Opens or closes the debug UI, as its toggle key does; from the next frame if a frame is underway.
    /// Nothing happens if it is disabled.
    void setDebugUiOpen(bool open);
    /// The debug UI's log console (its "Log" window): every message logged since the App was initialized,
    /// up to the last 2000. The debug UI must be enabled (asserted).
    [[nodiscard]] debugui::LogConsole& logConsole();
#endif

private:
    void start(Result<std::unique_ptr<platform::Platform>> platform);
    Result<void> initialize(Result<std::unique_ptr<platform::Platform>> platform);
    // Mounts AppConfig::mounts and the defaults on the platform's file system.
    Result<void> mountAssets();
    // One frame of the main loop: beginFrame(), the poll, finishFrame().
    void runFrame(const AppCallbacks& callbacks);
    // The frame boundary (scene switch, Startup) and a fresh input state and event queue.
    void beginFrame();
    // Handles the frame's events, updates, renders and ends the frame; nothing if no frame is open.
    void finishFrame(const AppCallbacks& callbacks);
    void updateAndRender(const AppCallbacks& callbacks);
    // The platform's live-resize handler: finishes the frame whose poll is underway and begins the next.
    void liveResizeFrame(const AppCallbacks& callbacks);
    void render(const AppCallbacks& callbacks, scene::Scene* active);
    // Whether the window has no image to render into: a device with a swapchain of zero size.
    [[nodiscard]] bool hasNoImage() const noexcept;
#if TESERRA_WITH_DEBUG_UI
    // Whether `event` is the debug UI's (its toggle key, or input its windows use), so the game does not
    // see it. Feeds it to the debug UI while open.
    [[nodiscard]] bool debugUiTakes(const platform::Event& event);
    // Starts the debug UI's frame if it is open.
    void beginDebugUiFrame();
    // Draws its windows and ends its frame, if one was started.
    void endDebugUiFrame();
    // Draws the debug UI's frame over `output`, if this frame built one.
    void renderDebugUi(const renderer::FrameTarget& output, rhi::CommandList& commands);
    void drawStatsWindow();
#endif

    AppConfig m_config;
    std::optional<Error> m_initError;
    // Services in dependency order: members are destroyed in reverse, the device before the window
    // before the platform.
    std::unique_ptr<platform::Platform> m_platform;
    // Reads through the platform's file system, so it is destroyed before the platform.
    std::optional<assets::MountTable> m_mounts;
    std::unique_ptr<platform::Window> m_window;
    std::unique_ptr<rhi::Device> m_device;
    // After the device: assets hold GPU objects of it. Before the scenes, whose components hold assets.
    std::optional<assets::AssetRegistry> m_assets;
    // After the device, so it is destroyed before it.
    std::unique_ptr<renderer::Renderer> m_renderer;
    bool m_rendererFailed = false;
    // The meshes of tilemap chunks of the active scene, built during its Extract; after the device.
    std::unique_ptr<renderer::ChunkMeshCache> m_chunkMeshes;
    // After the device: scenes are destroyed first, so GPU resources they hold go before the device.
    scene::SceneManager m_scenes;
    rhi::TextureHandle m_backbuffer;
    platform::Input m_input;
    platform::EventQueue m_events;
    platform::FrameTimer m_frameTimer;
    platform::FixedStepAccumulator m_fixedStep;
    u64 m_frameCount = 0;
    bool m_frameOpen = false; // between beginFrame() and finishFrame()
    bool m_paused = false;
    bool m_quitting = false;
    int m_exitCode = 0;
#if TESERRA_WITH_DEBUG_UI
    // Frame times in milliseconds for the Stats window, a ring of the last kFrameTimeCount frames.
    static constexpr usize kFrameTimeCount = 120;
    std::unique_ptr<debugui::DebugUi> m_debugUi;
    std::unique_ptr<debugui::LogConsole> m_logConsole;
    // An overlay-only renderer that draws the debug UI over the frame, whoever drew the frame; after the
    // device, created the first time the debug UI is drawn.
    std::unique_ptr<renderer::Renderer> m_debugRenderer;
    debugui::ImGuiPass* m_imguiPass = nullptr; // owned by m_debugRenderer
    bool m_debugRendererFailed = false;
    bool m_debugUiOpen = false;
    bool m_debugUiBuilt = false;   // this frame's debug UI frame ended, with draw data to render
    bool m_dropToggleText = false; // the text of the toggle key, which follows its press
    std::array<f32, kFrameTimeCount> m_frameTimes{};
    usize m_frameTimeNext = 0;
    usize m_frameTimesRecorded = 0;
    u32 m_lastFixedSteps = 0;
#endif
};

} // namespace teserra::engine
