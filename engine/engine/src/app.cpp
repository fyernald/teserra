#include <teserra/engine/app.hpp>

#include <teserra/core/assert.hpp>
#include <teserra/core/log.hpp>
#include <teserra/core/profile.hpp>

#if TESERRA_HAS_RHI_OPENGL
#include <teserra/rhi_opengl/opengl_backend.hpp>
#endif

#include <charconv>
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
    std::vector<rhi::BackendFactory> backends;
#if TESERRA_HAS_RHI_OPENGL
    backends.push_back(rhi::opengl::backendFactory());
#endif
    return backends;
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
        ++m_frameCount;
        if (m_config.maxFrames != 0 && m_frameCount >= m_config.maxFrames && !m_quitting) {
            quit();
        }
    }

    TESERRA_LOG_INFO(LogCategory::App, "'{}' ended after {} frames with exit code {}", m_config.title,
                     m_frameCount, m_exitCode);
    return m_exitCode;
}

void App::runFrame(const AppCallbacks& callbacks) {
    TESERRA_PROFILE_FRAME_MARK();

    m_input.beginFrame();
    m_events.clear();
    m_platform->pollEvents(m_events);
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

    const u32 steps = m_fixedStep.advance(m_frameTimer.delta());
    if (m_fixedStep.lastDroppedSteps() != 0) {
        TESERRA_LOG_WARN(LogCategory::App, "Frame took {} ms: skipped {} fixed steps to catch up",
                         std::chrono::duration_cast<std::chrono::milliseconds>(m_frameTimer.delta()).count(),
                         m_fixedStep.lastDroppedSteps());
    }
    if (callbacks.fixedUpdate) {
        for (u32 step = 0; step < steps; ++step) {
            callbacks.fixedUpdate(*this, m_fixedStep.stepSeconds());
        }
    }

    if (callbacks.update) {
        callbacks.update(*this, m_frameTimer.deltaSeconds());
    }

    render(callbacks);
}

void App::render(const AppCallbacks& callbacks) {
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
        if (callbacks.render) {
            callbacks.render(*this, commands);
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

void App::quit(int exitCode) noexcept {
    m_quitting = true;
    m_exitCode = exitCode;
}

platform::Platform& App::platform() {
    TESERRA_ASSERT(isInitialized(), "The App failed to initialize: {}", *m_initError);
    return *m_platform;
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
