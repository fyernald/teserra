// The swapchain and render pass replay of the OpenGL backend, checked as the GL calls they make on the fake
// driver of fake_gl.hpp.

#include <teserra/rhi/device.hpp>
#include <teserra/rhi_opengl/opengl_backend.hpp>
#include <teserra/test/assert.hpp>

#include "fake_gl.hpp"
#include "gl_format.hpp"
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

using Calls = std::vector<std::string>;

// Without RHI validation, so that the tests see the backend's own checks in every build type
// (opengl_buffer_test.cpp tests the backend with validation).
std::unique_ptr<Device> requireOpenGl(test::FakeGlWindow& window, const SwapchainDesc& swapchain = {}) {
    Result<std::unique_ptr<Device>> device = createDevice({.backend = Backend::OpenGL,
                                                           .enableValidation = false,
                                                           .enableBackendDebug = false,
                                                           .swapchain = swapchain},
                                                          &window, std::array{opengl::backendFactory()});
    if (!device) {
        FAIL(std::format("{}", device.error()));
    }
    return std::move(device).value();
}

// Records `record` into a command list of a new frame, submits it and returns the GL calls of the submit.
template <typename Record>
Calls submitFrame(Device& device, test::FakeGlState& gl, Record&& record) {
    device.beginFrame();
    CommandList& commands = device.acquireCommandList();
    record(commands);
    gl.calls.clear();
    device.submit(commands);
    Calls calls = gl.calls;
    device.endFrame();
    return calls;
}

} // namespace

TEST_CASE("OpenGL swapchain: an sRGB image of the framebuffer size", "[rhi_opengl][swapchain]") {
    test::FakeGlWindow window(true, {320, 200});
    const auto device = requireOpenGl(window);
    Swapchain& swapchain = *device->swapchain();

    CHECK(swapchain.size() == UVec2{320, 200});
    CHECK(swapchain.format() == Format::Rgba8Srgb);
    CHECK_FALSE(swapchain.acquireNextImage().isNull());
    CHECK(window.context.state.calls == Calls{
                                            "Enable(GL_FRAMEBUFFER_SRGB)",
                                            "SwapInterval(1)",
                                            "CreateTextures(GL_TEXTURE_2D) = 1",
                                            "TextureStorage2D(1, 1, GL_SRGB8_ALPHA8, 320x200)",
                                            "ObjectLabel(GL_TEXTURE, 1, Swapchain image)",
                                        });
}

TEST_CASE("OpenGL swapchain: formats and present modes", "[rhi_opengl][swapchain]") {
    test::FakeGlWindow window;

    SECTION("BGRA is stored as RGBA of the same encoding") {
        const auto device = requireOpenGl(window, {.format = Format::Bgra8Unorm});
        CHECK(device->swapchain()->format() == Format::Rgba8Unorm);
    }
    SECTION("a format the swapchain does not offer falls back to Rgba8Srgb") {
        const auto device = requireOpenGl(window, {.format = Format::R32Float});
        CHECK(device->swapchain()->format() == Format::Rgba8Srgb);
    }
    SECTION("Immediate turns vsync off") {
        const auto device = requireOpenGl(window, {.presentMode = PresentMode::Immediate});
        CHECK(std::ranges::count(window.context.state.calls, std::string("SwapInterval(0)")) == 1);
    }

    CHECK(opengl::swapchainFormat(Format::Rgba16Float) == Format::Rgba16Float);
    CHECK(opengl::swapchainFormat(Format::Bgra8Srgb) == Format::Rgba8Srgb);
    CHECK(opengl::glInternalFormat(Format::Rgba8Srgb) == GL_SRGB8_ALPHA8);
    CHECK(opengl::glInternalFormat(Format::Depth32Float) == GL_DEPTH_COMPONENT32F);
}

TEST_CASE("OpenGL swapchain: a clear pass and present", "[rhi_opengl][swapchain]") {
    test::FakeGlWindow window(true, {320, 200});
    const auto device = requireOpenGl(window);
    test::FakeGlState& gl = window.context.state;
    const TextureHandle image = device->swapchain()->acquireNextImage();

    const auto clearFrame = [&](CommandList& cmd) {
        cmd.pushDebugGroup("Clear");
        cmd.beginRenderPass({.colorAttachments = {{
                                 .texture = image,
                                 .loadOp = LoadOp::Clear,
                                 .clearColor = {0.25F, 0.5F, 0.75F, 1.0F},
                             }}});
        cmd.endRenderPass();
        cmd.popDebugGroup();
    };

    CHECK(submitFrame(*device, gl, clearFrame) ==
          Calls{
              "PushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, Clear)",
              "CreateFramebuffers() = 2",
              "NamedFramebufferTexture(2, GL_COLOR_ATTACHMENT0, 1, 0)",
              "NamedFramebufferDrawBuffers(2, GL_COLOR_ATTACHMENT0)",
              "BindFramebuffer(GL_DRAW_FRAMEBUFFER, 2)",
              "Disable(GL_SCISSOR_TEST)",
              "Viewport(0, 0 0 320 200)",
              "DepthRange(0 1)",
              "ClearNamedFramebufferfv(2, GL_COLOR, 0, 0.25 0.5 0.75 1)",
              "Disable(GL_SCISSOR_TEST)",
              "PopDebugGroup()",
          });

    // The framebuffer is cached for the next frame.
    const Calls second = submitFrame(*device, gl, clearFrame);
    CHECK(std::ranges::count(second, std::string("CreateFramebuffers() = 2")) == 0);
    CHECK(std::ranges::count(second,
                             std::string("ClearNamedFramebufferfv(2, GL_COLOR, 0, 0.25 0.5 0.75 1)")) == 1);

    // present() flips the image vertically into the default framebuffer, with sRGB conversion off.
    gl.calls.clear();
    device->swapchain()->present();
    CHECK(gl.calls ==
          Calls{
              "Disable(GL_FRAMEBUFFER_SRGB)",
              "Disable(GL_SCISSOR_TEST)",
              "BlitNamedFramebuffer(2 -> 0, 0 0 320 200 -> 0 200 320 0, GL_COLOR_BUFFER_BIT, GL_NEAREST)",
              "Enable(GL_FRAMEBUFFER_SRGB)",
              "SwapBuffers()",
          });
}

TEST_CASE("OpenGL swapchain: DontCare loads and stores invalidate", "[rhi_opengl][swapchain]") {
    test::FakeGlWindow window;
    const auto device = requireOpenGl(window);
    const TextureHandle image = device->swapchain()->acquireNextImage();

    const Calls calls = submitFrame(*device, window.context.state, [&](CommandList& cmd) {
        cmd.beginRenderPass({.colorAttachments = {{
                                 .texture = image,
                                 .loadOp = LoadOp::DontCare,
                                 .storeOp = StoreOp::DontCare,
                             }}});
        cmd.endRenderPass();
    });

    CHECK(std::ranges::count(calls, std::string("InvalidateNamedFramebufferData(2, GL_COLOR_ATTACHMENT0)")) ==
          2);
    CHECK(
        std::ranges::none_of(calls, [](const std::string& call) { return call.starts_with("ClearNamed"); }));
}

TEST_CASE("OpenGL swapchain: viewport and scissor keep the top-left origin", "[rhi_opengl][swapchain]") {
    test::FakeGlWindow window;
    const auto device = requireOpenGl(window);
    const TextureHandle image = device->swapchain()->acquireNextImage();

    const Calls calls = submitFrame(*device, window.context.state, [&](CommandList& cmd) {
        cmd.beginRenderPass({.colorAttachments = {{.texture = image}}});
        cmd.setViewport({.x = 10.0F, .y = 20.0F, .width = 100.0F, .height = 50.0F, .maxDepth = 0.5F});
        cmd.setScissor({.x = 1, .y = 2, .width = 30, .height = 40});
        cmd.endRenderPass();
    });

    // GL_UPPER_LEFT clip control makes window y count from the top row, so nothing is flipped.
    CHECK(std::ranges::count(calls, std::string("Viewport(0, 10 20 100 50)")) == 1);
    CHECK(std::ranges::count(calls, std::string("DepthRange(0 0.5)")) == 1);
    CHECK(std::ranges::count(calls, std::string("Enable(GL_SCISSOR_TEST)")) == 1);
    CHECK(std::ranges::count(calls, std::string("Scissor(1 2 30 40)")) == 1);
    CHECK(calls.back() == "Disable(GL_SCISSOR_TEST)"); // the end of the pass resets the scissor
}

TEST_CASE("OpenGL swapchain: resizing replaces the image", "[rhi_opengl][swapchain]") {
    test::FakeGlWindow window(true, {320, 200});
    const auto device = requireOpenGl(window);
    test::FakeGlState& gl = window.context.state;
    Swapchain& swapchain = *device->swapchain();
    const TextureHandle first = swapchain.acquireNextImage();
    static_cast<void>(submitFrame(*device, gl, [&](CommandList& cmd) {
        cmd.beginRenderPass({.colorAttachments = {{.texture = first, .loadOp = LoadOp::Clear}}});
        cmd.endRenderPass();
    }));

    gl.calls.clear();
    swapchain.resize({640, 480});
    const TextureHandle second = swapchain.acquireNextImage();
    CHECK(second != first);
    CHECK(swapchain.size() == UVec2{640, 480});
    CHECK(gl.calls == Calls{
                          "DeleteFramebuffers(2)",
                          "DeleteTextures(1)",
                          "CreateTextures(GL_TEXTURE_2D) = 3",
                          "TextureStorage2D(3, 1, GL_SRGB8_ALPHA8, 640x480)",
                          "ObjectLabel(GL_TEXTURE, 3, Swapchain image)",
                      });

    gl.calls.clear();
    swapchain.resize({640, 480}); // same size: nothing to do
    CHECK(gl.calls.empty());

    // Minimized: no image until the window has a size again.
    swapchain.resize({0, 0});
    CHECK(swapchain.acquireNextImage().isNull());
    swapchain.resize({800, 600});
    CHECK_FALSE(swapchain.acquireNextImage().isNull());
    CHECK(swapchain.size() == UVec2{800, 600});
}

TEST_CASE("OpenGL device: destruction deletes its GL objects", "[rhi_opengl][swapchain]") {
    test::FakeGlWindow window;
    {
        const auto device = requireOpenGl(window);
        const TextureHandle image = device->swapchain()->acquireNextImage();
        static_cast<void>(submitFrame(*device, window.context.state, [&](CommandList& cmd) {
            cmd.beginRenderPass({.colorAttachments = {{.texture = image, .loadOp = LoadOp::Clear}}});
            cmd.endRenderPass();
        }));
        CHECK(window.context.state.liveTextures.size() == 1);
        CHECK(window.context.state.liveFramebuffers.size() == 1);
    }
    CHECK(window.context.state.liveTextures.empty());
    CHECK(window.context.state.liveFramebuffers.empty());
}

#if TESERRA_ENABLE_ASSERTS
TEST_CASE("OpenGL swapchain: stale images and presenting without one assert", "[rhi_opengl][swapchain]") {
    test::FakeGlWindow window;
    const auto device = requireOpenGl(window);
    Swapchain& swapchain = *device->swapchain();
    const TextureHandle stale = swapchain.acquireNextImage();
    swapchain.resize({64, 64});

    device->beginFrame();
    CommandList& commands = device->acquireCommandList();
    commands.beginRenderPass({.colorAttachments = {{.texture = stale, .loadOp = LoadOp::Clear}}});
    commands.endRenderPass();
    const auto failure = test::requireAssert([&] { device->submit(commands); });
    CHECK(failure.message() == std::format("Color attachment 0 uses texture {}, which is not alive", stale));
    device->endFrame();

    swapchain.resize({0, 0});
    REQUIRE_ASSERTS(swapchain.present());
}
#endif
