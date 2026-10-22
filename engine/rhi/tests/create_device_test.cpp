// createDevice() picks the factory of the requested backend among those the build offers. The fake device
// and swapchain here also show that the interfaces can be implemented without a GPU.

#include <teserra/platform/window.hpp>
#include <teserra/rhi/device.hpp>
#include <teserra/test/assert.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <memory>
#include <string_view>

using namespace teserra;
using namespace teserra::rhi;

namespace {

class FakeSwapchain final : public Swapchain {
public:
    explicit FakeSwapchain(const SwapchainDesc& desc)
        : m_format(desc.format) {}

    [[nodiscard]] TextureHandle acquireNextImage() override { return TextureHandle{0, 1}; }
    void present() override { ++presented; }
    void resize(UVec2 framebufferSize) override { m_size = framebufferSize; }
    [[nodiscard]] UVec2 size() const noexcept override { return m_size; }
    [[nodiscard]] Format format() const noexcept override { return m_format; }

    int presented = 0;

private:
    UVec2 m_size{640, 480};
    Format m_format;
};

class FakeDevice final : public Device {
public:
    FakeDevice(Backend backend, const DeviceDesc& desc, platform::Window* window)
        : createdFor(window), m_backend(backend) {
        if (window != nullptr) {
            m_swapchain = std::make_unique<FakeSwapchain>(desc.swapchain);
        }
    }

    [[nodiscard]] Backend backend() const noexcept override { return m_backend; }
    [[nodiscard]] const DeviceLimits& limits() const noexcept override { return m_limits; }

    [[nodiscard]] Result<BufferHandle> createBuffer(const BufferDesc& /*desc*/) override {
        return BufferHandle{0, 1};
    }
    [[nodiscard]] Result<TextureHandle> createTexture(const TextureDesc& /*desc*/) override {
        return TextureHandle{1, 1};
    }
    [[nodiscard]] Result<SamplerHandle> createSampler(const SamplerDesc& /*desc*/) override {
        return SamplerHandle{0, 1};
    }
    [[nodiscard]] Result<ShaderModuleHandle> createShaderModule(const ShaderModuleDesc& desc) override {
        return fail(ErrorCode::CompilationFailed, Module::Rhi, "Cannot compile '{}'", desc.debugName);
    }
    [[nodiscard]] Result<GraphicsPipelineHandle>
    createGraphicsPipeline(const GraphicsPipelineDesc& /*desc*/) override {
        return GraphicsPipelineHandle{0, 1};
    }
    void destroy(BufferHandle /*handle*/) override {}
    void destroy(TextureHandle /*handle*/) override {}
    void destroy(SamplerHandle /*handle*/) override {}
    void destroy(ShaderModuleHandle /*handle*/) override {}
    void destroy(GraphicsPipelineHandle /*handle*/) override {}

    void beginFrame() override {}
    void endFrame() override {}
    // Command lists arrive with #26; nothing here records commands.
    [[nodiscard]] CommandList& acquireCommandList() override { TESERRA_UNREACHABLE(); }
    void submit(CommandList& /*commands*/) override {}
    [[nodiscard]] Swapchain* swapchain() noexcept override { return m_swapchain.get(); }
    void waitIdle() override {}

    platform::Window* createdFor;

private:
    Backend m_backend;
    DeviceLimits m_limits{};
    std::unique_ptr<FakeSwapchain> m_swapchain;
};

Result<std::unique_ptr<Device>> createFakeOpenGl(const DeviceDesc& desc, platform::Window* window) {
    if (window == nullptr) {
        return fail(ErrorCode::Unsupported, Module::Rhi, "OpenGL needs a window");
    }
    return std::make_unique<FakeDevice>(Backend::OpenGL, desc, window);
}

Result<std::unique_ptr<Device>> createFakeNull(const DeviceDesc& desc, platform::Window* window) {
    return std::make_unique<FakeDevice>(Backend::Null, desc, window);
}

constexpr std::array kBackends{
    BackendFactory{.backend = Backend::OpenGL, .create = &createFakeOpenGl},
    BackendFactory{.backend = Backend::Null, .create = &createFakeNull},
};

// Only its address is used: the fake devices never call into the window.
class UnusedWindow final : public platform::Window {
public:
    [[nodiscard]] UVec2 windowSize() const override { return {}; }
    [[nodiscard]] UVec2 framebufferSize() const override { return {}; }
    [[nodiscard]] Vec2 contentScale() const override { return Vec2{1.0F}; }
    void setSize(UVec2 /*size*/) override {}
    void setTitle(std::string_view /*title*/) override {}
    [[nodiscard]] bool shouldClose() const override { return false; }
    [[nodiscard]] bool isFocused() const override { return false; }
    [[nodiscard]] platform::NativeWindowHandle nativeHandle() const override { return {}; }
    [[nodiscard]] platform::OpenGlContext* openGlContext() override { return nullptr; }
};

} // namespace

TEST_CASE("createDevice: uses the factory of the requested backend", "[rhi][device]") {
    UnusedWindow window;

    auto device = createDevice({.backend = Backend::OpenGL}, &window, kBackends);

    REQUIRE(device.has_value());
    CHECK((*device)->backend() == Backend::OpenGL);
    CHECK(static_cast<FakeDevice&>(**device).createdFor == &window);
    REQUIRE((*device)->swapchain() != nullptr);
}

TEST_CASE("createDevice: passes the descriptor on", "[rhi][device]") {
    UnusedWindow window;

    auto device = createDevice({.backend = Backend::Null, .swapchain = {.format = Format::Bgra8Srgb}},
                               &window, kBackends);

    REQUIRE(device.has_value());
    REQUIRE((*device)->swapchain() != nullptr);
    CHECK((*device)->swapchain()->format() == Format::Bgra8Srgb);
}

TEST_CASE("createDevice: a headless device has no swapchain", "[rhi][device]") {
    auto device = createDevice({.backend = Backend::Null}, nullptr, kBackends);

    REQUIRE(device.has_value());
    CHECK((*device)->swapchain() == nullptr);
}

TEST_CASE("createDevice: a backend that is not built in is unsupported", "[rhi][device]") {
    const auto device = createDevice({.backend = Backend::Vulkan}, nullptr, kBackends);

    REQUIRE_FALSE(device.has_value());
    CHECK(device.error().code == ErrorCode::Unsupported);
    CHECK(device.error().module == Module::Rhi);
    CHECK(device.error().message == "The Vulkan backend is not part of this build");
}

TEST_CASE("createDevice: errors of the backend are returned", "[rhi][device]") {
    const auto device = createDevice({.backend = Backend::OpenGL}, nullptr, kBackends);

    REQUIRE_FALSE(device.has_value());
    CHECK(device.error().message == "OpenGL needs a window");
}

#if TESERRA_ENABLE_ASSERTS
TEST_CASE("createDevice: each backend may be offered once", "[rhi][device]") {
    constexpr std::array kTwice{
        BackendFactory{.backend = Backend::Null, .create = &createFakeNull},
        BackendFactory{.backend = Backend::Null, .create = &createFakeNull},
    };

    REQUIRE_ASSERTS(static_cast<void>(createDevice({.backend = Backend::Null}, nullptr, kTwice)));
}
#endif

TEST_CASE("Device: creation errors and resource handles", "[rhi][device]") {
    auto device = createDevice({.backend = Backend::Null}, nullptr, kBackends);
    REQUIRE(device.has_value());

    const auto texture = (*device)->createTexture({.width = 64, .height = 64, .debugName = "Atlas"});
    REQUIRE(texture.has_value());
    CHECK_FALSE(texture->isNull());

    const auto shader =
        (*device)->createShaderModule({.stage = ShaderStage::Fragment, .debugName = "Broken"});
    REQUIRE_FALSE(shader.has_value());
    CHECK(shader.error().code == ErrorCode::CompilationFailed);
}

TEST_CASE("backendName: names every backend", "[rhi][device]") {
    CHECK(backendName(Backend::OpenGL) == "OpenGL");
    CHECK(backendName(Backend::Vulkan) == "Vulkan");
    CHECK(backendName(Backend::Null) == "Null");
}
