#include <teserra/engine/app.hpp>

#include <teserra/assets/image.hpp>
#include <teserra/assets/tiled.hpp>
#include <teserra/core/assert.hpp>
#include <teserra/core/log.hpp>
#include <teserra/core/profile.hpp>
#include <teserra/renderer/assets.hpp>
#include <teserra/renderer/debug_pass.hpp>
#include <teserra/renderer/sprite_pass.hpp>
#include <teserra/rhi_null/null_device.hpp>
#include <teserra/scene/render_extraction.hpp>
#if TESERRA_HAS_RHI_OPENGL
#include <teserra/rhi_opengl/opengl_backend.hpp>
#endif
#if TESERRA_WITH_DEBUG_UI
#include <teserra/debug_ui/imgui_pass.hpp>
#endif

#include <algorithm>
#include <array>
#include <charconv>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace teserra::engine {

namespace {

// The audio engine of `settings`: the default sound device, or offline output when audio is disabled or there
// is no device (a warning: the game runs without sound).
Result<std::unique_ptr<audio::AudioEngine>> createAudio(const AudioSettings& settings) {
    audio::AudioEngineConfig config{.output = audio::Output::Device,
                                    .sampleRate = settings.sampleRate,
                                    .channels = settings.channels,
                                    .maxVoices = settings.maxVoices};
    if (settings.enabled) {
        Result<std::unique_ptr<audio::AudioEngine>> device = audio::AudioEngine::create(config);
        if (device || device.error().code != ErrorCode::DeviceFailure) {
            return device;
        }
        TESERRA_LOG_WARN(LogCategory::Audio, "No sound: {}. The game runs without it.",
                         device.error().message);
    }
    config.output = audio::Output::Offline;
    return audio::AudioEngine::create(config);
}

// Applies the engine flags in config.args (see AppConfig) and leaves every other argument to the game.
Result<void> applyCommandLine(AppConfig& config) {
    const CommandLine& args = config.args;
    for (int i = 1; i < args.argc; ++i) {
        const std::string_view arg = args.argv[i];
        if (arg == "--hidden-window") {
            config.windowVisible = false;
        } else if (arg == "--debug-ui") {
            config.debugUi.startOpen = true;
        } else if (arg == "--no-audio") {
            config.audio.enabled = false;
        } else if (arg == "--frames") {
            if (i + 1 >= args.argc) {
                return fail(ErrorCode::InvalidArgument, Module::App, "--frames needs a frame count");
            }
            const std::string_view count = args.argv[++i];
            u64 frames = 0;
            const auto [end, error] = std::from_chars(count.data(), count.data() + count.size(), frames);
            if (error != std::errc{} || end != count.data() + count.size() || frames == 0) {
                return fail(ErrorCode::InvalidArgument, Module::App,
                            "--frames needs a positive frame count, got '{}'", count);
            }
            config.maxFrames = frames;
        }
    }
    return {};
}

// The RHI backends this build includes (CMake options TESERRA_RHI_*), for rhi::createDevice().
std::vector<rhi::BackendFactory> compiledBackends() {
    std::vector<rhi::BackendFactory> backends{rhi::null::backendFactory()};
#if TESERRA_HAS_RHI_OPENGL
    backends.push_back(rhi::opengl::backendFactory());
#endif
    return backends;
}

// The schemes mounted unless AppConfig::mounts lists them, each to `<executable dir>/<scheme>`.
constexpr std::array<std::string_view, 2> kDefaultMounts{"assets", "engine"};

// Whether `directory` is absolute: it starts with a separator ("/srv/game", "\\server\share") or a drive
// letter ("C:/Games"). Anything else is relative to the executable's directory.
[[nodiscard]] bool isAbsoluteDirectory(std::string_view directory) noexcept {
    if (directory.empty()) {
        return false;
    }
    if (directory.front() == '/' || directory.front() == '\\') {
        return true;
    }
    const char drive = directory.front();
    return directory.size() >= 2 && directory[1] == ':' &&
           ((drive >= 'A' && drive <= 'Z') || (drive >= 'a' && drive <= 'z'));
}

// The graphics context a window needs for `backend`.
platform::GraphicsApi graphicsApiFor(const std::optional<rhi::Backend>& backend) noexcept {
    return backend == rhi::Backend::OpenGL ? platform::GraphicsApi::OpenGL : platform::GraphicsApi::None;
}

} // namespace

App::App(AppConfig config)
    : m_config(std::move(config)), m_fixedStep(m_config.fixedStep, m_config.maxFixedStepsPerFrame) {
    start(platform::Platform::create());
}

App::App(AppConfig config, std::unique_ptr<platform::Platform> platform)
    : m_config(std::move(config)), m_fixedStep(m_config.fixedStep, m_config.maxFixedStepsPerFrame) {
    if (platform == nullptr) {
        start(fail(ErrorCode::InvalidArgument, Module::App, "No platform was given to the App"));
    } else {
        start(std::move(platform));
    }
}

App::~App() = default;

void App::start(Result<std::unique_ptr<platform::Platform>> platform) {
    if (Result<void> initialized = initialize(std::move(platform)); !initialized) {
        TESERRA_LOG_ERROR(LogCategory::App, "Cannot start '{}': {}", m_config.title, initialized.error());
        m_initError = std::move(initialized).error();
    }
}

Result<void> App::initialize(Result<std::unique_ptr<platform::Platform>> platform) {
    // The App owns the platform from here on, even if a later step fails.
    TESERRA_TRY_ASSIGN(m_platform, std::move(platform));
    TESERRA_TRY(applyCommandLine(m_config));
    TESERRA_TRY(mountAssets());

    TESERRA_TRY_ASSIGN(m_window, m_platform->createWindow({
                                     .title = m_config.title,
                                     .size = m_config.windowSize,
                                     .resizable = m_config.resizable,
                                     .visible = m_config.windowVisible,
                                     .graphicsApi = graphicsApiFor(m_config.backend),
                                 }));

    if (m_config.backend) {
        const rhi::DeviceDesc desc{
            .backend = *m_config.backend,
            .swapchain = {.presentMode =
                              m_config.vsync ? rhi::PresentMode::Fifo : rhi::PresentMode::Immediate},
        };
        TESERRA_TRY_ASSIGN(m_device, rhi::createDevice(desc, m_window.get(), compiledBackends()));
    }

    m_assets.emplace(assets::AssetRegistryConfig{.mounts = &*m_mounts});
    m_assets->registerLoader<assets::ImageData>(std::make_unique<assets::ImageLoader>());
    m_assets->registerLoader<assets::TiledMap>(std::make_unique<assets::TiledMapLoader>());
    if (m_device) {
        TESERRA_TRY(renderer::registerAssetLoaders(*m_assets, *m_device));
    }
    audio::registerAssetLoaders(*m_assets);
    TESERRA_TRY_ASSIGN(m_audio, createAudio(m_config.audio));

#if TESERRA_WITH_DEBUG_UI
    if (m_config.debugUi.enabled) {
        TESERRA_TRY_ASSIGN(m_debugUi, debugui::DebugUi::create({.contentScale = m_window->contentScale().x}));
        m_debugUi->addWindow("Stats", [this] { drawStatsWindow(); });
        m_logConsole = std::make_unique<debugui::LogConsole>();
        m_debugUi->addWindow("Log", [this] { m_logConsole->draw(); }, /*open=*/false);
        m_debugUi->addWindow(
            "Entities", [this] { m_entityInspector.drawEntityList(m_scenes); }, /*open=*/false);
        m_debugUi->addWindow(
            "Inspector", [this] { m_entityInspector.drawInspector(m_scenes); }, /*open=*/false);
        m_debugUi->addWindow("Assets", [this] { m_assetList.draw(*m_assets); }, /*open=*/false);
        m_debugUi->addWindow("Renderer", [this] { drawRendererWindow(); }, /*open=*/false);
        setDebugUiOpen(m_config.debugUi.startOpen);
    }
#endif
    return {};
}

int App::run(const AppCallbacks& callbacks) {
    if (!isInitialized()) {
        return 1;
    }
    TESERRA_LOG_INFO(LogCategory::App, "Running '{}'", m_config.title);

    TESERRA_PROFILE_THREAD_NAME("Main");
    // While the window system resizes the window inside a poll (a border dragged on Windows), frames are
    // drawn from there, so the window shows the game at every size it passes through (#184).
    m_platform->setLiveResizeHandler([this, &callbacks] { liveResizeFrame(callbacks); });
    m_frameTimer.tick(m_platform->clock().now()); // the first frame's delta starts here
    while (!m_quitting) {
        runFrame(callbacks);
    }
    m_platform->setLiveResizeHandler({});

    TESERRA_LOG_INFO(LogCategory::App, "'{}' ended after {} frames with exit code {}", m_config.title,
                     m_frameCount, m_exitCode);
    return m_exitCode;
}

int App::run(scene::Scene& scene, const AppCallbacks& callbacks) {
    TESERRA_ASSERT(m_scenes.owns(scene), "App::run() needs a scene created with App::scenes()");
    m_scenes.switchTo(scene);
    m_scenes.beginFrame(); // active before the first frame
    return run(callbacks);
}

void App::runFrame(const AppCallbacks& callbacks) {
    beginFrame();
    {
        TESERRA_PROFILE_SCOPE("Poll events");
        if (m_paused) {
            m_platform->waitEvents(m_events, m_config.minimizedEventWait); // nothing to draw: do not spin
        } else {
            m_platform->pollEvents(m_events);
        }
    }
    // A live resize during the poll may have finished frames of its own; this finishes the last one it
    // began, with the events that came after.
    finishFrame(callbacks);
}

void App::beginFrame() {
    TESERRA_PROFILE_FRAME_MARK();
    // The frame boundary: a scene switch requested during the previous frame takes effect here. The active
    // scene stays the same for the rest of the frame, whatever is requested during it.
    m_scenes.beginFrame();
    if (scene::Scene* const active = m_scenes.active()) {
        active->run(ecs::Phase::Startup); // only the first frame this scene runs
    }
    m_input.beginFrame();
    m_events.clear();
    m_frameOpen = true;
}

void App::liveResizeFrame(const AppCallbacks& callbacks) {
    if (!m_frameOpen) {
        return; // a frame finished from here quit
    }
    // The frame whose poll is underway, with the events so far, ending with the resize; then the next
    // one, whose events the poll goes on collecting.
    finishFrame(callbacks);
    if (!m_quitting) {
        beginFrame();
    }
}

void App::finishFrame(const AppCallbacks& callbacks) {
    if (!m_frameOpen) {
        return;
    }
    m_frameOpen = false;
    updateAndRender(callbacks);
    m_audio->update();
    // After the frame's work was submitted: the device keeps GPU objects past the frames in flight.
    {
        TESERRA_PROFILE_SCOPE("Assets end frame");
        m_assets->endFrame();
    }
    ++m_frameCount;
    if (m_config.maxFrames != 0 && m_frameCount >= m_config.maxFrames && !m_quitting) {
        quit();
    }
}

void App::updateAndRender(const AppCallbacks& callbacks) {
    scene::Scene* const active = m_scenes.active();
    const auto runPhase = [active](ecs::Phase phase) {
        if (active != nullptr) {
            active->run(phase);
        }
    };

    {
        TESERRA_PROFILE_SCOPE("Events");
        for (const platform::Event& event : m_events) {
#if TESERRA_WITH_DEBUG_UI
            if (debugUiTakes(event)) {
                continue;
            }
#endif
            m_input.processEvent(event);
            if (callbacks.onEvent) {
                callbacks.onEvent(*this, event);
            }
            if (std::holds_alternative<platform::WindowCloseRequested>(event) && !m_quitting) {
                quit();
            }
            if (const auto* resized = std::get_if<platform::WindowResized>(&event)) {
                if (rhi::Swapchain* swapchain = m_device ? m_device->swapchain() : nullptr) {
                    swapchain->resize(resized->framebufferSize);
                }
            }
        }
    }

    m_frameTimer.tick(m_platform->clock().now());
    // Without an image, the frame stops here: the simulation pauses until the window is restored.
    m_paused = hasNoImage();
    if (m_paused) {
        return;
    }
#if TESERRA_WITH_DEBUG_UI
    m_frameTimes[m_frameTimeNext] = m_frameTimer.deltaSeconds() * 1000.0F;
    m_frameTimeNext = (m_frameTimeNext + 1) % kFrameTimeCount;
    m_frameTimesRecorded = std::min(m_frameTimesRecorded + 1, kFrameTimeCount);
    beginDebugUiFrame();
#endif
    if (active != nullptr) {
        active->time().advance(m_frameTimer.deltaSeconds(), m_fixedStep.stepSeconds());
        ecs::World& world = active->world();
        if (platform::Input* input = world.tryResource<platform::Input>()) {
            *input = m_input; // keeps the capacity of its text
        } else {
            world.insertResource<platform::Input>(m_input);
        }
        if (world.tryResource<audio::Audio>() == nullptr) {
            world.insertResource<audio::Audio>(audio::Audio{.engine = m_audio.get()});
        }
    }
    runPhase(ecs::Phase::PreUpdate);

    const u32 steps = m_fixedStep.advance(m_frameTimer.delta());
#if TESERRA_WITH_DEBUG_UI
    m_lastFixedSteps = steps;
#endif
    if (m_fixedStep.lastDroppedSteps() != 0) {
        TESERRA_LOG_WARN(LogCategory::App, "Frame took {} ms: skipped {} fixed steps to catch up",
                         std::chrono::duration_cast<std::chrono::milliseconds>(m_frameTimer.delta()).count(),
                         m_fixedStep.lastDroppedSteps());
    }
    for (u32 step = 0; step < steps; ++step) {
        runPhase(ecs::Phase::FixedUpdate);
        if (callbacks.fixedUpdate) {
            callbacks.fixedUpdate(*this, m_fixedStep.stepSeconds());
        }
    }

    runPhase(ecs::Phase::Update);
    if (callbacks.update) {
        callbacks.update(*this, m_frameTimer.deltaSeconds());
    }
    runPhase(ecs::Phase::PostUpdate);
    if (active != nullptr) {
        if (auto* extraction = active->world().tryResource<scene::RenderExtraction>()) {
            const rhi::Swapchain* swapchain = m_device ? m_device->swapchain() : nullptr;
            const UVec2 outputSize = swapchain != nullptr ? swapchain->size() : UVec2{0U};
            // The App's renderer draws the scene unless the game renders itself.
            const renderer::Renderer* drawer = m_device && !callbacks.render ? renderer() : nullptr;
            extraction->targetSize = drawer != nullptr ? drawer->sceneSize(outputSize) : outputSize;
            extraction->renderer = drawer;
            if (m_device && !m_chunkMeshes) {
                m_chunkMeshes = std::make_unique<renderer::ChunkMeshCache>(*m_device);
            }
            extraction->chunkMeshes = m_chunkMeshes.get();
        }
    }
    runPhase(ecs::Phase::Extract);
#if TESERRA_WITH_DEBUG_UI
    endDebugUiFrame();
#endif

    render(callbacks, active);
    runPhase(ecs::Phase::Last);
}

void App::render(const AppCallbacks& callbacks, scene::Scene* active) {
    if (!m_device) {
        return;
    }
    TESERRA_PROFILE_SCOPE("Render");
    rhi::Device& device = *m_device;
    rhi::Swapchain* swapchain = device.swapchain();
    device.beginFrame();
    m_backbuffer = swapchain != nullptr ? swapchain->acquireNextImage() : rhi::TextureHandle{};
    // Without an image (minimized window), there is nothing to render into or present this frame.
    if (!m_backbuffer.isNull()) {
        rhi::CommandList& commands = device.acquireCommandList();
        const scene::RenderExtraction* extraction =
            active != nullptr ? active->world().tryResource<scene::RenderExtraction>() : nullptr;
        renderer::Renderer* sceneDrawer = !callbacks.render && extraction != nullptr ? renderer() : nullptr;
        if (callbacks.render) {
            callbacks.render(*this, commands);
        } else if (sceneDrawer != nullptr) {
            sceneDrawer->render(
                extraction->packet,
                {.texture = m_backbuffer, .format = swapchain->format(), .size = swapchain->size()},
                commands);
        } else {
            commands.beginRenderPass(
                {.colorAttachments = {{.texture = m_backbuffer, .loadOp = rhi::LoadOp::Clear}}});
            commands.endRenderPass();
        }
#if TESERRA_WITH_DEBUG_UI
        renderDebugUi({.texture = m_backbuffer, .format = swapchain->format(), .size = swapchain->size()},
                      commands);
#endif
        device.submit(commands);
        // With vsync, presenting is what paces the loop.
        TESERRA_PROFILE_SCOPE("Present");
        swapchain->present();
    }
    m_backbuffer = {};
    device.endFrame();
}

bool App::hasNoImage() const noexcept {
    const rhi::Swapchain* swapchain = m_device ? m_device->swapchain() : nullptr;
    return swapchain != nullptr && (swapchain->size().x == 0 || swapchain->size().y == 0);
}

renderer::Renderer* App::renderer() {
    TESERRA_ASSERT(m_device != nullptr, "App::renderer() needs a device (AppConfig::backend)");
    if (m_renderer != nullptr || m_rendererFailed || m_device == nullptr) {
        return m_renderer.get();
    }
    const auto create = [&]() -> Result<std::unique_ptr<renderer::Renderer>> {
        TESERRA_TRY_ASSIGN(std::unique_ptr<renderer::Renderer> created,
                           renderer::Renderer::create(*m_device, {.scene = m_config.sceneTarget}));
        TESERRA_TRY_ASSIGN(
            std::unique_ptr<renderer::SpritePass> sprites,
            renderer::SpritePass::create(*m_device, created->frameGroupLayout(), m_config.spritePass));
#if TESERRA_WITH_DEBUG_UI
        m_spritePass = sprites.get();
#endif
        created->addPass(std::move(sprites));
        TESERRA_TRY_ASSIGN(std::unique_ptr<renderer::DebugPass> debug,
                           renderer::DebugPass::create(*m_device, created->frameGroupLayout()));
#if TESERRA_WITH_DEBUG_UI
        m_debugPass = debug.get();
#endif
        created->addPass(std::move(debug));
        return created;
    };
    if (Result<std::unique_ptr<renderer::Renderer>> created = create(); created) {
        m_renderer = std::move(*created);
    } else {
        // A game that cannot draw its scene ends with an error instead of showing a black window.
        TESERRA_LOG_ERROR(LogCategory::App, "Cannot create the scene renderer: {}", created.error());
        m_rendererFailed = true;
        quit(1);
    }
    return m_renderer.get();
}

void App::quit(int exitCode) noexcept {
    m_quitting = true;
    m_exitCode = exitCode;
}

Result<void> App::mountAssets() {
    platform::FileSystem& fileSystem = m_platform->fileSystem();
    assets::MountTable& mounts = m_mounts.emplace(fileSystem);
    TESERRA_TRY_ASSIGN(const std::string executableDirectory, fileSystem.executableDirectory());

    for (const AssetMount& mount : m_config.mounts) {
        // An empty directory stays empty, so that mount() reports it.
        const std::string directory = mount.directory.empty() || isAbsoluteDirectory(mount.directory)
                                          ? mount.directory
                                          : executableDirectory + '/' + mount.directory;
        TESERRA_TRY(mounts.mount(mount.scheme, directory));
        if (!fileSystem.exists(directory)) {
            return fail(ErrorCode::NotFound, Module::App,
                        "The directory '{}' mounted as '{}://' does not exist", directory, mount.scheme);
        }
    }
    for (const std::string_view scheme : kDefaultMounts) {
        if (!mounts.directory(scheme).has_value()) {
            TESERRA_TRY(mounts.mount(scheme, executableDirectory + '/' + std::string(scheme)));
        }
    }
    return {};
}

platform::Platform& App::platform() {
    TESERRA_ASSERT(isInitialized(), "The App failed to initialize: {}", *m_initError);
    return *m_platform;
}

assets::AssetRegistry& App::assets() {
    TESERRA_ASSERT(isInitialized(), "The App failed to initialize: {}", *m_initError);
    return *m_assets;
}

audio::AudioEngine& App::audio() {
    TESERRA_ASSERT(isInitialized(), "The App failed to initialize: {}", *m_initError);
    return *m_audio;
}

assets::MountTable& App::mounts() {
    TESERRA_ASSERT(isInitialized(), "The App failed to initialize: {}", *m_initError);
    return *m_mounts;
}

platform::Window& App::window() {
    TESERRA_ASSERT(isInitialized(), "The App failed to initialize: {}", *m_initError);
    return *m_window;
}

#if TESERRA_WITH_DEBUG_UI
debugui::DebugUi& App::debugUi() {
    TESERRA_ASSERT(isInitialized(), "The App failed to initialize: {}", *m_initError);
    TESERRA_ASSERT(m_debugUi != nullptr, "The debug UI is disabled: AppConfig::debugUi.enabled is false");
    return *m_debugUi;
}

debugui::LogConsole& App::logConsole() {
    TESERRA_ASSERT(isInitialized(), "The App failed to initialize: {}", *m_initError);
    TESERRA_ASSERT(m_logConsole != nullptr, "The debug UI is disabled: AppConfig::debugUi.enabled is false");
    return *m_logConsole;
}

void App::setDebugUiOpen(bool open) {
    if (m_debugUi == nullptr || open == m_debugUiOpen) {
        return;
    }
    m_debugUiOpen = open;
    if (open) {
        // ImGui saw no input while closed: keys and buttons it thinks held are released, and the cursor is
        // where the game's input last saw it.
        m_debugUi->processEvent(platform::FocusChanged{.focused = false});
        m_debugUi->processEvent(platform::FocusChanged{.focused = true});
        m_debugUi->processEvent(platform::MouseMoved{.position = m_input.mousePosition()});
    }
}

bool App::debugUiTakes(const platform::Event& event) {
    if (m_debugUi == nullptr) {
        return false;
    }
    const platform::Key toggleKey = m_config.debugUi.toggleKey;
    if (const auto* key = std::get_if<platform::KeyChanged>(&event);
        key != nullptr && toggleKey != platform::Key::Unknown && key->key == toggleKey) {
        if (key->down && !key->repeat) {
            setDebugUiOpen(!m_debugUiOpen);
            m_dropToggleText = true;
        }
        return true;
    }
    // The text the toggle key typed comes right after its press.
    if (std::exchange(m_dropToggleText, false) && std::holds_alternative<platform::TextEntered>(event)) {
        return true;
    }
    if (!m_debugUiOpen) {
        return false;
    }
    m_debugUi->processEvent(event);
    // As of the debug UI's last frame, like ImGui's own backends. Releases always reach the game, so that
    // nothing it saw pressed stays held.
    const bool mouse = m_debugUi->wantsMouse();
    const bool keyboard = m_debugUi->wantsKeyboard();
    if (const auto* button = std::get_if<platform::MouseButtonChanged>(&event)) {
        return mouse && button->down;
    }
    if (std::holds_alternative<platform::MouseScrolled>(event)) {
        return mouse;
    }
    if (const auto* key = std::get_if<platform::KeyChanged>(&event)) {
        return keyboard && key->down;
    }
    if (std::holds_alternative<platform::TextEntered>(event)) {
        return keyboard;
    }
    return false;
}

void App::beginDebugUiFrame() {
    m_debugUiBuilt = false;
    if (m_debugUi == nullptr || !m_debugUiOpen) {
        return;
    }
    TESERRA_PROFILE_SCOPE("Debug UI begin frame");
    const UVec2 windowSize = m_window->windowSize();
    const UVec2 framebufferSize = m_window->framebufferSize();
    const Vec2 scale =
        windowSize.x != 0 && windowSize.y != 0 ? Vec2(framebufferSize) / Vec2(windowSize) : Vec2{1.0F};
    m_debugUi->beginFrame(Vec2(windowSize), scale, m_frameTimer.deltaSeconds());
}

void App::endDebugUiFrame() {
    if (m_debugUi == nullptr || !m_debugUi->inFrame()) {
        return;
    }
    TESERRA_PROFILE_SCOPE("Debug UI end frame");
    if (m_debugUiOpen) { // the game may have closed it during the frame
        m_debugUi->drawWindows();
    }
    m_debugUi->endFrame();
    m_debugUiBuilt = m_debugUiOpen;
}

void App::renderDebugUi(const renderer::FrameTarget& output, rhi::CommandList& commands) {
    if (!m_debugUiBuilt || m_debugRendererFailed) {
        return;
    }
    if (m_debugRenderer == nullptr) {
        const auto create = [&]() -> Result<void> {
            // ImGui's vertices and indices; a busy debug UI uses a few hundred kilobytes a frame.
            TESERRA_TRY_ASSIGN(
                m_debugRenderer,
                renderer::Renderer::create(*m_device, {.uploadBytesPerFrame = u64{2} * 1024 * 1024}));
            TESERRA_TRY_ASSIGN(std::unique_ptr<debugui::ImGuiPass> pass,
                               debugui::ImGuiPass::create(*m_device));
            m_imguiPass = pass.get();
            m_debugRenderer->addOverlayPass(std::move(pass));
            return {};
        };
        if (Result<void> created = create(); !created) {
            TESERRA_LOG_ERROR(LogCategory::App, "Cannot create the debug UI's renderer: {}", created.error());
            m_debugRenderer.reset();
            m_imguiPass = nullptr;
            m_debugRendererFailed = true;
            return;
        }
    }
    TESERRA_PROFILE_SCOPE("Debug UI");
    m_imguiPass->setDrawData(m_debugUi->drawData());
    m_debugRenderer->renderOverlays(output, commands);
}

void App::drawStatsWindow() {
    const usize count = m_frameTimesRecorded;
    f32 total = 0.0F;
    f32 longest = 0.0F;
    for (usize i = 0; i < count; ++i) {
        total += m_frameTimes[i];
        longest = std::max(longest, m_frameTimes[i]);
    }
    const f32 average = count != 0 ? total / static_cast<f32>(count) : 0.0F;
    ImGui::Text("%.0f FPS (%.2f ms)", static_cast<double>(average > 0.0F ? 1000.0F / average : 0.0F),
                static_cast<double>(average));
    // Oldest first: once the ring is full, the next slot holds the oldest time.
    const int offset = count == kFrameTimeCount ? static_cast<int>(m_frameTimeNext) : 0;
    ImGui::PlotLines("##frame times", m_frameTimes.data(), static_cast<int>(count), offset, "frame ms", 0.0F,
                     std::max(longest * 1.25F, 1000.0F / 30.0F), ImVec2(0.0F, ImGui::GetFontSize() * 4.0F));
    ImGui::Text("Fixed steps this frame: %u", m_lastFixedSteps);
    if (const scene::Scene* active = m_scenes.active()) {
        ImGui::Text("Scene: %.*s, %llu entities", static_cast<int>(active->name().size()),
                    active->name().data(), static_cast<unsigned long long>(active->world().entityCount()));
    } else {
        ImGui::TextUnformatted("Scene: none");
    }
    const UVec2 windowSize = m_window->windowSize();
    const UVec2 framebufferSize = m_window->framebufferSize();
    ImGui::Text("Window %u x %u, framebuffer %u x %u", windowSize.x, windowSize.y, framebufferSize.x,
                framebufferSize.y);
    const char* output = m_audio->output() == audio::Output::Device ? "sound device"
                         : m_config.audio.enabled                   ? "no sound device"
                                                                    : "audio off";
    ImGui::Text("Audio: %u of %u voices, %s", m_audio->voiceCount(), m_audio->maxVoices(), output);
}

void App::drawRendererWindow() {
    if (m_renderer == nullptr || m_spritePass == nullptr || m_debugPass == nullptr) {
        ImGui::TextWrapped(
            "The App's scene renderer has not drawn: the game renders with a render callback, or "
            "there is no device.");
    } else {
        const renderer::RendererStats& stats = m_renderer->stats();
        ImGui::SeparatorText("Frame");
        ImGui::Text("Views: %u, camera render targets: %u", stats.views, stats.renderTargets);
        ImGui::Text("Scene target: %u x %u", stats.sceneSize.x, stats.sceneSize.y);
        const renderer::SpritePassStats& sprites = m_spritePass->stats();
        ImGui::SeparatorText("Sprite pass");
        ImGui::Text("Sprites: %u in %u batches", sprites.sprites, sprites.batches);
        ImGui::Text("Tilemap chunks: %u", sprites.tilemapChunks);
        ImGui::Text("Draw calls: %u", sprites.drawCalls);
        ImGui::Text("Material pipelines: %llu",
                    static_cast<unsigned long long>(m_spritePass->materialPipelineCount()));
        const renderer::DebugPassStats& debug = m_debugPass->stats();
        ImGui::SeparatorText("Debug pass");
        ImGui::Text("Lines: %u, draw calls: %u", debug.lines, debug.drawCalls);
    }
    if (m_imguiPass != nullptr) {
        const debugui::ImGuiPassStats& imgui = m_imguiPass->stats();
        ImGui::SeparatorText("Debug UI pass");
        ImGui::Text("Draw calls: %u, vertices: %u, textures: %u", imgui.drawCalls, imgui.vertices,
                    imgui.textures);
    }
}
#endif

rhi::Device& App::device() {
    TESERRA_ASSERT(isInitialized(), "The App failed to initialize: {}", *m_initError);
    TESERRA_ASSERT(m_device != nullptr, "The App has no device: AppConfig::backend is std::nullopt");
    return *m_device;
}

} // namespace teserra::engine
