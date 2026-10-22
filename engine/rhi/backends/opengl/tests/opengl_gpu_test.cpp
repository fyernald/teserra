// The OpenGL device on a real driver, in a hidden window. Skipped on machines without a window system or an
// OpenGL 4.5 driver, such as the CI runners; opengl_device_test.cpp covers the same paths with a fake driver.

#include <teserra/platform/opengl_context.hpp>
#include <teserra/platform/platform.hpp>
#include <teserra/rhi/device.hpp>
#include <teserra/rhi_opengl/opengl_backend.hpp>
#include <teserra/test/log_capture.hpp>

#include "gl_device.hpp"
#include <catch2/catch_test_macros.hpp>
#include <glad/gl.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <format>
#include <memory>
#include <ranges>
#include <span>
#include <vector>

using namespace teserra;
using namespace teserra::rhi;

namespace {

struct GpuWindow {
    std::unique_ptr<platform::Platform> platform;
    std::unique_ptr<platform::Window> window;
};

GpuWindow createGpuWindowOrSkip() {
    Result<std::unique_ptr<platform::Platform>> platform = platform::Platform::create();
    if (!platform) {
        SKIP("No window system: " << std::format("{}", platform.error()));
    }
    Result<std::unique_ptr<platform::Window>> window = (*platform)->createWindow({
        .title = "Teserra OpenGL test",
        .size = {320, 200},
        .visible = false,
        .graphicsApi = platform::GraphicsApi::OpenGL,
        .debugContext = true,
    });
    if (!window) {
        SKIP("No OpenGL 4.5 driver: " << std::format("{}", window.error()));
    }
    return {std::move(platform).value(), std::move(window).value()};
}

GLADapiproc loadFunction(void* context, const char* name) {
    return static_cast<platform::OpenGlContext*>(context)->getProcAddress(name);
}

} // namespace

TEST_CASE("OpenGL backend (GPU): creates a device on the window's context", "[rhi_opengl][gpu]") {
    const GpuWindow gpu = createGpuWindowOrSkip();
    test::LogCapture log;

    auto created = createDevice({.backend = Backend::OpenGL, .enableBackendDebug = true}, gpu.window.get(),
                                std::array{opengl::backendFactory()});
    REQUIRE(created.has_value());
    Device& device = **created;
    CHECK(device.backend() == Backend::OpenGL);

    const DeviceLimits minimums{};
    CHECK(device.limits().maxTextureDimension2D >= minimums.maxTextureDimension2D);
    CHECK(device.limits().maxTextureArrayLayers >= minimums.maxTextureArrayLayers);
    CHECK(device.limits().uniformBufferOffsetAlignment <= minimums.uniformBufferOffsetAlignment);
    CHECK(device.limits().maxUniformBufferRange >= minimums.maxUniformBufferRange);
    CHECK(device.limits().maxSamplerAnisotropy >= 1.0F);

    // The test's own function table, to look at the state the device set up.
    platform::OpenGlContext& context = *gpu.window->openGlContext();
    GladGLContext gl{};
    REQUIRE(gladLoadGLContextUserPtr(&gl, &loadFunction, &context) != 0);

    GLint clipOrigin = 0;
    GLint clipDepthMode = 0;
    gl.GetIntegerv(GL_CLIP_ORIGIN, &clipOrigin);
    gl.GetIntegerv(GL_CLIP_DEPTH_MODE, &clipDepthMode);
    CHECK(clipOrigin == GL_UPPER_LEFT);
    CHECK(clipDepthMode == GL_ZERO_TO_ONE);

    log.clear();
    gl.DebugMessageInsert(GL_DEBUG_SOURCE_APPLICATION, GL_DEBUG_TYPE_ERROR, 42, GL_DEBUG_SEVERITY_HIGH, -1,
                          "Inserted by the test");
    bool logged = false;
    for (const test::CapturedLog& record : log.records()) {
        logged = logged || (record.level == LogLevel::Error && record.category == LogCategory::Rhi &&
                            record.message == "OpenGL Error 42 from Application: Inserted by the test");
    }
    CHECK(logged);

    device.beginFrame();
    device.submit(device.acquireCommandList());
    device.endFrame();
    device.waitIdle();
}

TEST_CASE("OpenGL backend (GPU): clears and presents frames", "[rhi_opengl][gpu]") {
    const GpuWindow gpu = createGpuWindowOrSkip();
    const test::LogCapture log;
    auto created =
        createDevice({.backend = Backend::OpenGL, .enableValidation = true, .enableBackendDebug = true},
                     gpu.window.get(), std::array{opengl::backendFactory()});
    REQUIRE(created.has_value());
    auto& device = static_cast<opengl::GlDevice&>(**created);
    Swapchain& swapchain = *device.swapchain();
    REQUIRE(swapchain.format() == Format::Rgba8Srgb);

    for (int frame = 0; frame < 10; ++frame) {
        device.beginFrame();
        const TextureHandle image = swapchain.acquireNextImage();
        REQUIRE_FALSE(image.isNull());
        CommandList& cmd = device.acquireCommandList();
        cmd.pushDebugGroup("Clear");
        cmd.beginRenderPass({.colorAttachments = {{
                                 .texture = image,
                                 .loadOp = LoadOp::Clear,
                                 .clearColor = {0.5F, 0.25F, 1.0F, 1.0F}, // linear
                             }}});
        cmd.endRenderPass();
        cmd.popDebugGroup();
        device.submit(cmd);

        if (frame == 0) {
            // The image holds the sRGB encoding of the linear clear color: 0.5 -> 188, 0.25 -> 137.
            std::array<GLubyte, 4> texel{};
            device.gl().GetTextureSubImage(device.texture(image)->name, 0, 0, 0, 0, 1, 1, 1, GL_RGBA,
                                           GL_UNSIGNED_BYTE, static_cast<GLsizei>(texel.size()),
                                           texel.data());
            CHECK(std::abs(texel[0] - 188) <= 1);
            CHECK(std::abs(texel[1] - 137) <= 1);
            CHECK(texel[2] == 255);
            CHECK(texel[3] == 255);
        }
        swapchain.present();
        device.endFrame();
    }

    // Resizing and minimizing: a new image, then none, then one again.
    swapchain.resize({64, 48});
    CHECK(swapchain.size() == UVec2{64, 48});
    swapchain.resize({0, 0});
    CHECK(swapchain.acquireNextImage().isNull());
    swapchain.resize({320, 200});
    CHECK_FALSE(swapchain.acquireNextImage().isNull());
    device.waitIdle();

    for (const test::CapturedLog& record : log.records()) {
        INFO(record.message);
        CHECK(record.level < LogLevel::Warn);
    }
}

TEST_CASE("OpenGL backend (GPU): buffer uploads round trip through readback", "[rhi_opengl][gpu]") {
    const GpuWindow gpu = createGpuWindowOrSkip();
    const test::LogCapture log;
    auto created =
        createDevice({.backend = Backend::OpenGL, .enableValidation = true, .enableBackendDebug = true},
                     gpu.window.get(), std::array{opengl::backendFactory()});
    REQUIRE(created.has_value());
    Device& device = **created;

    constexpr usize kSize = 1024;
    std::vector<std::byte> pattern(kSize);
    for (usize i = 0; i < kSize; ++i) {
        pattern[i] = static_cast<std::byte>((i * 7) & 0xFFU);
    }
    const BufferHandle gpuOnly = *device.createBuffer({.sizeInBytes = kSize,
                                                       .usage = BufferUsage::CopySrc | BufferUsage::CopyDst,
                                                       .debugName = "Round trip: GpuOnly"});
    const BufferHandle upload = *device.createBuffer({.sizeInBytes = kSize,
                                                      .usage = BufferUsage::CopySrc,
                                                      .memory = MemoryLocation::Upload,
                                                      .debugName = "Round trip: Upload"});
    const BufferHandle readback = *device.createBuffer({.sizeInBytes = 2 * kSize,
                                                        .usage = BufferUsage::CopyDst,
                                                        .memory = MemoryLocation::Readback,
                                                        .debugName = "Round trip: Readback"});

    device.writeBuffer(gpuOnly, 0, pattern);
    std::ranges::reverse_copy(pattern, device.map(upload).begin());

    device.beginFrame();
    CommandList& cmd = device.acquireCommandList();
    cmd.copyBuffer({.source = gpuOnly, .destination = readback, .size = kSize});
    cmd.copyBuffer({.source = upload, .destination = readback, .destinationOffset = kSize, .size = kSize});
    device.submit(cmd);
    device.endFrame();
    device.waitIdle();

    const std::span<const std::byte> result = device.map(readback);
    REQUIRE(result.size() == 2 * kSize);
    CHECK(std::ranges::equal(result.first(kSize), pattern));
    CHECK(std::ranges::equal(result.last(kSize), std::views::reverse(pattern)));

    device.destroy(gpuOnly);
    device.destroy(upload);
    device.destroy(readback);
    for (const test::CapturedLog& record : log.records()) {
        INFO(record.message);
        CHECK(record.level < LogLevel::Warn);
    }
}
