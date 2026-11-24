#include <teserra/engine/app.hpp>

#include <teserra/assets/image.hpp>
#include <teserra/core/assert.hpp>
#include <teserra/core/log.hpp>
#include <teserra/core/profile.hpp>
#include <teserra/renderer/assets.hpp>
#include <teserra/renderer/sprite_pass.hpp>
#include <teserra/rhi_null/null_device.hpp>
#include <teserra/scene/render_extraction.hpp>
#if TESERRA_HAS_RHI_OPENGL
#include <teserra/rhi_opengl/opengl_backend.hpp>
#endif

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

// Applies the engine flags in config.args (see AppConfig) and leaves every other argument to the game.
Result<void> applyCommandLine(AppConfig& config) {
    const CommandLine& args = config.args;
    for (int i = 1; i < args.argc; ++i) {
        const std::string_view arg = args.argv[i];
        if (arg == "--hidden-window") {
            config.windowVisible = false;
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
    if (m_device) {
        TESERRA_TRY(renderer::registerAssetLoaders(*m_assets, *m_device));
    }
    return {};
}

int App::run(const AppCallbacks& callbacks) {
    if (!isInitialized()) {
        return 1;
    }
    TESERRA_LOG_INFO(LogCategory::App, "Running '{}'", m_config.title);

    m_frameTimer.tick(m_platform->clock().now()); // the first frame's delta starts here
    while (!m_quitting) {
        runFrame(callbacks);
        // After the frame's work was submitted: the device keeps GPU objects past the frames in flight.
        m_assets->endFrame();
        ++m_frameCount;
        if (m_config.maxFrames != 0 && m_frameCount >= m_config.maxFrames && !m_quitting) {
            quit();
        }
    }

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
    TESERRA_PROFILE_FRAME_MARK();

    // The frame boundary: a scene switch requested during the previous frame takes effect here. The active
    // scene stays the same for the rest of the frame, whatever is requested during it.
    m_scenes.beginFrame();
    scene::Scene* const active = m_scenes.active();
    const auto runPhase = [active](ecs::Phase phase) {
        if (active != nullptr) {
            active->run(phase);
        }
    };
    runPhase(ecs::Phase::Startup); // only the first frame this scene runs

    m_input.beginFrame();
    m_events.clear();
    if (m_paused) {
        m_platform->waitEvents(m_events, m_config.minimizedEventWait); // nothing to draw: do not spin
    } else {
        m_platform->pollEvents(m_events);
    }
    for (const platform::Event& event : m_events) {
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

    m_frameTimer.tick(m_platform->clock().now());
    // Without an image, the frame stops here: the simulation pauses until the window is restored.
    m_paused = hasNoImage();
    if (m_paused) {
        return;
    }
    if (active != nullptr) {
        active->time().advance(m_frameTimer.deltaSeconds(), m_fixedStep.stepSeconds());
        ecs::World& world = active->world();
        if (platform::Input* input = world.tryResource<platform::Input>()) {
            *input = m_input; // keeps the capacity of its text
        } else {
            world.insertResource<platform::Input>(m_input);
        }
    }
    runPhase(ecs::Phase::PreUpdate);

    const u32 steps = m_fixedStep.advance(m_frameTimer.delta());
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
            extraction->targetSize = swapchain != nullptr ? swapchain->size() : UVec2{0U};
            if (m_device && !m_chunkMeshes) {
                m_chunkMeshes = std::make_unique<renderer::ChunkMeshCache>(*m_device);
            }
            extraction->chunkMeshes = m_chunkMeshes.get();
        }
    }
    runPhase(ecs::Phase::Extract);

    render(callbacks, active);
    runPhase(ecs::Phase::Last);
}

void App::render(const AppCallbacks& callbacks, scene::Scene* active) {
    if (!m_device) {
        return;
    }
    rhi::Device& device = *m_device;
    rhi::Swapchain* swapchain = device.swapchain();
    device.beginFrame();
    m_backbuffer = swapchain != nullptr ? swapchain->acquireNextImage() : rhi::TextureHandle{};
    // Without an image (minimized window), there is nothing to render into or present this frame.
    if (!m_backbuffer.isNull()) {
        rhi::CommandList& commands = device.acquireCommandList();
        const scene::RenderExtraction* extraction =
            active != nullptr ? active->world().tryResource<scene::RenderExtraction>() : nullptr;
        renderer::Renderer* sceneDrawer =
            !callbacks.render && extraction != nullptr ? sceneRenderer() : nullptr;
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
        device.submit(commands);
        // With vsync, presenting is what paces the loop.
        swapchain->present();
    }
    m_backbuffer = {};
    device.endFrame();
}

bool App::hasNoImage() const noexcept {
    const rhi::Swapchain* swapchain = m_device ? m_device->swapchain() : nullptr;
    return swapchain != nullptr && (swapchain->size().x == 0 || swapchain->size().y == 0);
}

renderer::Renderer* App::sceneRenderer() {
    if (m_renderer != nullptr || m_rendererFailed) {
        return m_renderer.get();
    }
    const auto create = [&]() -> Result<std::unique_ptr<renderer::Renderer>> {
        TESERRA_TRY_ASSIGN(std::unique_ptr<renderer::Renderer> created,
                           renderer::Renderer::create(*m_device));
        TESERRA_TRY_ASSIGN(std::unique_ptr<renderer::SpritePass> sprites,
                           renderer::SpritePass::create(*m_device, created->frameGroupLayout()));
        created->addPass(std::move(sprites));
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

assets::MountTable& App::mounts() {
    TESERRA_ASSERT(isInitialized(), "The App failed to initialize: {}", *m_initError);
    return *m_mounts;
}

platform::Window& App::window() {
    TESERRA_ASSERT(isInitialized(), "The App failed to initialize: {}", *m_initError);
    return *m_window;
}

rhi::Device& App::device() {
    TESERRA_ASSERT(isInitialized(), "The App failed to initialize: {}", *m_initError);
    TESERRA_ASSERT(m_device != nullptr, "The App has no device: AppConfig::backend is std::nullopt");
    return *m_device;
}

} // namespace teserra::engine
