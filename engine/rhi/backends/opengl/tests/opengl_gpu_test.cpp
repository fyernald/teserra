// The OpenGL device on a real driver, in a hidden window. Skipped on machines without a window system or an
// OpenGL 4.5 driver, such as the CI runners; opengl_device_test.cpp covers the same paths with a fake driver.

#include <teserra/platform/opengl_context.hpp>
#include <teserra/platform/platform.hpp>
#include <teserra/rhi/device.hpp>
#include <teserra/rhi_opengl/opengl_backend.hpp>
#include <teserra/test/log_capture.hpp>

#include <catch2/catch_test_macros.hpp>
#include <glad/gl.h>

#include <array>
#include <format>
#include <memory>

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
