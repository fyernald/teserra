// OpenGL device creation, debug output and the frame lifecycle, against the fake driver of fake_gl.hpp:
// these tests need no GPU and run in CI. opengl_gpu_test.cpp repeats the essentials on a real driver.

#include <teserra/rhi/device.hpp>
#include <teserra/rhi_opengl/opengl_backend.hpp>
#include <teserra/test/assert.hpp>
#include <teserra/test/log_capture.hpp>

#include "fake_gl.hpp"
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <memory>
#include <string>
#include <vector>

using namespace teserra;
using namespace teserra::rhi;

namespace {

const std::array kBackends{opengl::backendFactory()};

// Without RHI validation, so that the tests see the backend's own checks in every build type
// (opengl_buffer_test.cpp tests the backend with validation).
Result<std::unique_ptr<Device>> createOpenGl(test::FakeGlWindow& window, bool backendDebug = true) {
    return createDevice(
        {.backend = Backend::OpenGL, .enableValidation = false, .enableBackendDebug = backendDebug}, &window,
        kBackends);
}

std::unique_ptr<Device> requireOpenGl(test::FakeGlWindow& window, bool backendDebug = true) {
    Result<std::unique_ptr<Device>> device = createOpenGl(window, backendDebug);
    if (!device) {
        FAIL(std::format("{}", device.error()));
    }
    return std::move(device).value();
}

} // namespace

TEST_CASE("OpenGL backend: offered for Backend::OpenGL", "[rhi_opengl]") {
    CHECK(opengl::backendFactory().backend == Backend::OpenGL);
    CHECK(opengl::backendFactory().create != nullptr);
}

TEST_CASE("OpenGL backend: needs a window with an OpenGL context", "[rhi_opengl]") {
    const auto headless = createDevice({.backend = Backend::OpenGL}, nullptr, kBackends);
    REQUIRE_FALSE(headless.has_value());
    CHECK(headless.error().code == ErrorCode::Unsupported);
    CHECK(headless.error().message == "The OpenGL backend needs a window");

    test::FakeGlWindow window(/*withContext=*/false);
    const auto noContext = createOpenGl(window);
    REQUIRE_FALSE(noContext.has_value());
    CHECK(noContext.error().code == ErrorCode::Unsupported);
    CHECK(noContext.error().message == "The OpenGL backend needs a window created with GraphicsApi::OpenGL");
}

TEST_CASE("OpenGL backend: a context below 4.5 is unsupported", "[rhi_opengl]") {
    test::FakeGlWindow window;
    window.context.state.version = "3.3.0 Fake";

    const auto device = createOpenGl(window);

    REQUIRE_FALSE(device.has_value());
    CHECK(device.error().code == ErrorCode::Unsupported);
    CHECK(device.error().module == Module::Rhi);
    CHECK(device.error().message == "OpenGL 4.5 is required, but the context provides 3.3 (Fake renderer)");
}

TEST_CASE("OpenGL backend: functions that cannot be loaded fail creation", "[rhi_opengl]") {
    test::FakeGlWindow window;
    window.context.state.missingFunctions.insert("glGetString");

    const auto device = createOpenGl(window);

    REQUIRE_FALSE(device.has_value());
    CHECK(device.error().code == ErrorCode::DeviceFailure);
    CHECK(device.error().message == "Could not load the OpenGL functions");
}

TEST_CASE("OpenGL backend: limits below what the RHI needs are unsupported", "[rhi_opengl]") {
    test::FakeGlWindow window;

    SECTION("uniform buffer bindings") {
        window.context.state.integers[GL_MAX_UNIFORM_BUFFER_BINDINGS] = 24;
        const auto device = createOpenGl(window);
        REQUIRE_FALSE(device.has_value());
        CHECK(device.error().message ==
              "GL_MAX_UNIFORM_BUFFER_BINDINGS is 24, but the OpenGL backend needs at least 32");
    }
    SECTION("texture size") {
        window.context.state.integers[GL_MAX_TEXTURE_SIZE] = 4096;
        const auto device = createOpenGl(window);
        REQUIRE_FALSE(device.has_value());
        CHECK(device.error().message ==
              "GL_MAX_TEXTURE_SIZE is 4096, but the OpenGL backend needs at least 8192");
    }
    SECTION("uniform buffer offset alignment") {
        window.context.state.integers[GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT] = 512;
        const auto device = createOpenGl(window);
        REQUIRE_FALSE(device.has_value());
        CHECK(device.error().message ==
              "GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT is 512, but the OpenGL backend needs at most 256");
    }
}

TEST_CASE("OpenGL backend: a device with the RHI conventions and the driver's limits", "[rhi_opengl]") {
    test::FakeGlWindow window;
    const test::LogCapture log;

    const auto device = requireOpenGl(window);

    CHECK(device->backend() == Backend::OpenGL);
    const test::FakeGlState& gl = window.context.state;
    CHECK(gl.clipOrigin == GL_UPPER_LEFT);
    CHECK(gl.clipDepthMode == GL_ZERO_TO_ONE);

    const DeviceLimits& limits = device->limits();
    CHECK(limits.maxTextureDimension2D == 16384);
    CHECK(limits.maxTextureArrayLayers == 2048);
    CHECK(limits.uniformBufferOffsetAlignment == 64);
    CHECK(limits.maxUniformBufferRange == 65536);
    CHECK(limits.maxSamplerAnisotropy == 1.0F); // the driver lacks EXT_texture_filter_anisotropic

    bool announced = false;
    for (const test::CapturedLog& record : log.records()) {
        announced = announced ||
                    (record.category == LogCategory::Rhi && record.level == LogLevel::Info &&
                     record.message == "OpenGL device: Fake renderer (Teserra tests), OpenGL 4.6.0 Fake");
    }
    CHECK(announced);
}

TEST_CASE("OpenGL backend: anisotropy comes from the extension", "[rhi_opengl]") {
    test::FakeGlWindow window;
    window.context.state.extensions.emplace_back("GL_EXT_texture_filter_anisotropic");

    const auto device = requireOpenGl(window);

    CHECK(device->limits().maxSamplerAnisotropy == 16.0F);
}

TEST_CASE("OpenGL backend: debug output only with enableBackendDebug", "[rhi_opengl]") {
    test::FakeGlWindow window;

    SECTION("enabled") {
        const auto device = requireOpenGl(window, /*backendDebug=*/true);
        CHECK(window.context.state.enabled.contains(GL_DEBUG_OUTPUT));
        CHECK(window.context.state.enabled.contains(GL_DEBUG_OUTPUT_SYNCHRONOUS));
        CHECK(window.context.state.debugCallback != nullptr);
        // The driver's echoes of debug groups are turned off.
        const auto& calls = window.context.state.calls;
        CHECK(std::ranges::count(calls,
                                 std::string("DebugMessageControl(GL_DONT_CARE, GL_DEBUG_TYPE_PUSH_GROUP, "
                                             "GL_DONT_CARE, off)")) == 1);
        CHECK(std::ranges::count(calls,
                                 std::string("DebugMessageControl(GL_DONT_CARE, GL_DEBUG_TYPE_POP_GROUP, "
                                             "GL_DONT_CARE, off)")) == 1);
    }
    SECTION("disabled") {
        const auto device = requireOpenGl(window, /*backendDebug=*/false);
        CHECK_FALSE(window.context.state.enabled.contains(GL_DEBUG_OUTPUT));
        CHECK_FALSE(window.context.state.enabled.contains(GL_DEBUG_OUTPUT_SYNCHRONOUS));
        CHECK(window.context.state.debugCallback == nullptr);
    }
}

TEST_CASE("OpenGL backend: debug messages are logged at their mapped level", "[rhi_opengl]") {
    test::FakeGlWindow window;
    const auto device = requireOpenGl(window);
    const GLDEBUGPROC callback = window.context.state.debugCallback;
    REQUIRE(callback != nullptr);
    test::LogCapture log;

    callback(GL_DEBUG_SOURCE_API, GL_DEBUG_TYPE_ERROR, 1282, GL_DEBUG_SEVERITY_HIGH, -1,
             "GL_INVALID_OPERATION in glDrawArrays\n", nullptr);
    const std::string performance = "Program recompiled; extra text past the length";
    callback(GL_DEBUG_SOURCE_SHADER_COMPILER, GL_DEBUG_TYPE_PERFORMANCE, 7, GL_DEBUG_SEVERITY_MEDIUM, 18,
             performance.c_str(), nullptr);
    callback(GL_DEBUG_SOURCE_APPLICATION, GL_DEBUG_TYPE_OTHER, 3, GL_DEBUG_SEVERITY_LOW, -1, "Low", nullptr);

    REQUIRE(log.records().size() == 3);
    CHECK(log.records()[0].level == LogLevel::Error);
    CHECK(log.records()[0].category == LogCategory::Rhi);
    CHECK(log.records()[0].message == "OpenGL Error 1282 from API: GL_INVALID_OPERATION in glDrawArrays");
    CHECK(log.records()[1].level == LogLevel::Warn);
    CHECK(log.records()[1].message == "OpenGL Performance 7 from Shader compiler: Program recompiled");
    CHECK(log.records()[2].level == LogLevel::Info);
    CHECK(log.records()[2].message == "OpenGL Other 3 from Application: Low");
}

TEST_CASE("OpenGL backend: frames hand out command lists per frame slot", "[rhi_opengl]") {
    test::FakeGlWindow window;
    const auto device = requireOpenGl(window);

    for (int frame = 0; frame < 3; ++frame) {
        device->beginFrame();
        CommandList& first = device->acquireCommandList();
        CommandList& second = device->acquireCommandList();
        CHECK(&first != &second);
        CHECK(first.empty());
        CHECK_FALSE(first.isFinished()); // reset when the frame slot comes around again
        device->submit(first);
        CHECK(first.isFinished());
        device->endFrame();
    }

    device->waitIdle();
    CHECK(window.context.state.finishCalls == 1);
}

TEST_CASE("OpenGL backend: a frame waits for the fence of the frame that last used its slot",
          "[rhi_opengl][frames]") {
    test::FakeGlWindow window;
    const auto device = requireOpenGl(window);
    test::FakeGlState& gl = window.context.state;
    // The fence calls of `frames` frames.
    const auto syncCalls = [&](int frames) {
        gl.syncCalls.clear();
        for (int frame = 0; frame < frames; ++frame) {
            device->beginFrame();
            device->endFrame();
        }
        return gl.syncCalls;
    };
    // The name of the fence that a "FenceSync(...) = <name>" call created.
    const auto created = [](const std::string& call) { return call.substr(call.rfind(' ') + 1); };
    const auto fence = [](const std::string& name) {
        return std::format("FenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE) = {}", name);
    };

    SECTION("frames 0 and 1 place fences; frame 2 waits for frame 0's before reusing slot 0") {
        const std::vector<std::string> calls = syncCalls(3);
        REQUIRE(calls.size() == 5);
        const std::string first = created(calls[0]);
        CHECK(calls == std::vector<std::string>{
                           fence(first),
                           fence(created(calls[1])),
                           std::format("ClientWaitSync({}, GL_SYNC_FLUSH_COMMANDS_BIT)", first),
                           std::format("DeleteSync({})", first),
                           fence(created(calls[4])),
                       });
        CHECK(device->frameSlot() == 1);
        CHECK(gl.liveSyncs.size() == 2);

        device->waitIdle(); // every fence is signaled after glFinish()
        CHECK(gl.finishCalls == 1);
        CHECK(gl.liveSyncs.empty());
        const std::vector<std::string> next = syncCalls(1); // nothing left to wait for
        REQUIRE(next.size() == 1);
        CHECK(next[0].starts_with("FenceSync("));
    }
    SECTION("a frame still running is waited for without flushing again") {
        const std::string first = created(syncCalls(2)[0]);
        gl.waitResults = {GL_TIMEOUT_EXPIRED, GL_TIMEOUT_EXPIRED, GL_ALREADY_SIGNALED};
        const std::vector<std::string> calls = syncCalls(1);
        REQUIRE(calls.size() == 5);
        CHECK(calls == std::vector<std::string>{
                           std::format("ClientWaitSync({}, GL_SYNC_FLUSH_COMMANDS_BIT)", first),
                           std::format("ClientWaitSync({}, 0)", first),
                           std::format("ClientWaitSync({}, 0)", first),
                           std::format("DeleteSync({})", first),
                           fence(created(calls[4])),
                       });
    }
    SECTION("a failed wait is logged and the frame goes on") {
        static_cast<void>(syncCalls(2));
        gl.waitResults = {GL_WAIT_FAILED};
        const test::LogCapture log;
        CHECK(syncCalls(1).size() == 3); // the wait, the delete and the new fence
        REQUIRE(log.records().size() == 1);
        CHECK(log.records()[0].level == LogLevel::Error);
        CHECK(log.records()[0].message == "Waiting for the GPU to finish frame slot 0 failed");
    }
}

TEST_CASE("OpenGL backend: the device deletes the fences it still holds", "[rhi_opengl][frames]") {
    test::FakeGlWindow window;
    {
        const auto device = requireOpenGl(window);
        device->beginFrame();
        device->endFrame();
        CHECK(window.context.state.liveSyncs.size() == 1);
    }
    CHECK(window.context.state.liveSyncs.empty());
}

TEST_CASE("OpenGL backend: destroying null handles does nothing", "[rhi_opengl]") {
    test::FakeGlWindow window;
    const auto device = requireOpenGl(window);
    window.context.state.calls.clear();

    device->destroy(BufferHandle{});
    device->destroy(TextureHandle{});
    device->destroy(SamplerHandle{});
    device->destroy(ShaderModuleHandle{});
    device->destroy(BindGroupLayoutHandle{});
    device->destroy(PipelineLayoutHandle{});
    device->destroy(BindGroupHandle{});
    device->destroy(GraphicsPipelineHandle{});
    CHECK(window.context.state.calls.empty());
}

#if TESERRA_ENABLE_ASSERTS
TEST_CASE("OpenGL backend: frame and submission misuse asserts", "[rhi_opengl]") {
    test::FakeGlWindow window;
    const auto device = requireOpenGl(window);

    REQUIRE_ASSERTS(device->endFrame());
    REQUIRE_ASSERTS(static_cast<void>(device->acquireCommandList()));

    device->beginFrame();
    REQUIRE_ASSERTS(device->beginFrame());
    // A handle this device never gave out: replay catches it even without validation.
    CommandList& commands = device->acquireCommandList();
    commands.beginRenderPass({.colorAttachments = {{.texture = device->swapchain()->acquireNextImage()}}});
    commands.bindPipeline(GraphicsPipelineHandle{1, 1});
    commands.endRenderPass();
    const auto failure = test::requireAssert([&] { device->submit(commands); });
    CHECK(failure.message() == "bindPipeline uses pipeline 1:1, which is not alive");
    device->endFrame();

    REQUIRE_ASSERTS(device->destroy(BufferHandle{1, 1}));
}
#endif
