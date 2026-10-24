// OpenGL textures against the fake driver of fake_gl.hpp: storage, uploads, mip generation, buffer-to-texture
// copies and depth-stencil attachments, checked through the GL calls the device makes. opengl_gpu_test.cpp
// checks the texels on a real driver.

#include <teserra/rhi/device.hpp>
#include <teserra/rhi_opengl/opengl_backend.hpp>
#include <teserra/test/assert.hpp>

#include "fake_gl.hpp"
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace teserra;
using namespace teserra::rhi;

namespace {

std::unique_ptr<Device> requireOpenGl(test::FakeGlWindow& window) {
    Result<std::unique_ptr<Device>> device =
        createDevice({.backend = Backend::OpenGL, .enableValidation = true, .enableBackendDebug = false},
                     &window, std::array{opengl::backendFactory()});
    if (!device) {
        FAIL(std::format("{}", device.error()));
    }
    return std::move(device).value();
}

bool called(const test::FakeGlState& gl, std::string_view call) {
    return std::ranges::find(gl.calls, call) != gl.calls.end();
}

// The calls after the first one that starts with `prefix`, for logs whose start varies.
std::vector<std::string> callsFrom(const test::FakeGlState& gl, std::string_view prefix) {
    const auto first =
        std::ranges::find_if(gl.calls, [&](const std::string& call) { return call.starts_with(prefix); });
    return {first, gl.calls.end()};
}

} // namespace

TEST_CASE("OpenGL backend: textures get immutable storage for every mip level", "[rhi_opengl][texture]") {
    test::FakeGlWindow window;
    const auto device = requireOpenGl(window);
    test::FakeGlState& gl = window.context.state;
    const GLuint first = gl.lastName + 1;
    gl.calls.clear();

    const TextureHandle sprites = *device->createTexture({.format = Format::Rgba8Srgb,
                                                          .width = 64,
                                                          .height = 32,
                                                          .mipLevels = kAllMips,
                                                          .debugName = "Sprites"});
    const TextureHandle tiles = *device->createTexture({.dimension = TextureDimension::Tex2DArray,
                                                        .format = Format::R8Unorm,
                                                        .width = 16,
                                                        .height = 16,
                                                        .depthOrLayers = 4,
                                                        .debugName = "Tiles"});

    const std::vector<std::string> expected{
        std::format("CreateTextures(GL_TEXTURE_2D) = {}", first),
        // 64x32 down to 1x1: 7 levels.
        std::format("TextureStorage2D({}, 7, GL_SRGB8_ALPHA8, 64x32)", first),
        std::format("ObjectLabel(GL_TEXTURE, {}, Sprites)", first),
        std::format("CreateTextures(GL_TEXTURE_2D_ARRAY) = {}", first + 1),
        std::format("TextureStorage3D({}, 1, GL_R8, 16x16x4)", first + 1),
        std::format("ObjectLabel(GL_TEXTURE, {}, Tiles)", first + 1),
    };
    CHECK(gl.calls == expected);
    CHECK(gl.liveTextures.contains(first));

    gl.calls.clear();
    device->destroy(sprites);
    device->destroy(tiles);
    device->destroy(TextureHandle{}); // null handles are ignored
    CHECK(gl.calls == std::vector<std::string>{std::format("DeleteTextures({})", first),
                                               std::format("DeleteTextures({})", first + 1)});
    CHECK_FALSE(gl.liveTextures.contains(first));
}

TEST_CASE("OpenGL backend: texture creation reports what it cannot do", "[rhi_opengl][texture]") {
    test::FakeGlWindow window;
    const auto device = requireOpenGl(window);
    test::FakeGlState& gl = window.context.state;

    SECTION("out of memory") {
        const usize liveBefore = gl.liveTextures.size(); // the swapchain image
        gl.nextError = GL_OUT_OF_MEMORY;
        const auto texture = device->createTexture({.width = 8192, .height = 8192, .debugName = "Huge"});

        REQUIRE_FALSE(texture.has_value());
        CHECK(texture.error().code == ErrorCode::OutOfMemory);
        CHECK(texture.error().message == "Out of GPU memory for texture 'Huge' (8192x8192x1)");
        CHECK(gl.liveTextures.size() == liveBefore);
    }
    SECTION("3D, cube and multisampled textures are not in v1") {
        const auto volume = device->createTexture({.dimension = TextureDimension::Tex3D,
                                                   .width = 8,
                                                   .height = 8,
                                                   .depthOrLayers = 8,
                                                   .debugName = "Fog"});
        const auto sky = device->createTexture({.dimension = TextureDimension::Cube,
                                                .width = 8,
                                                .height = 8,
                                                .depthOrLayers = 6,
                                                .debugName = "Sky"});
        const auto msaa = device->createTexture({.format = Format::Rgba8Unorm,
                                                 .width = 8,
                                                 .height = 8,
                                                 .sampleCount = 4,
                                                 .usage = TextureUsage::RenderTarget,
                                                 .debugName = "Msaa"});

        REQUIRE_FALSE(volume.has_value());
        CHECK(volume.error().code == ErrorCode::Unsupported);
        CHECK(volume.error().message == "Texture 'Fog': the OpenGL backend has no 3D or cube textures in v1");
        REQUIRE_FALSE(sky.has_value());
        CHECK(sky.error().code == ErrorCode::Unsupported);
        REQUIRE_FALSE(msaa.has_value());
        CHECK(msaa.error().message ==
              "Texture 'Msaa': the OpenGL backend has no multisampled textures in v1");
    }
}

TEST_CASE("OpenGL backend: writeTexture uploads rows top first, with no flip", "[rhi_opengl][texture]") {
    test::FakeGlWindow window;
    const auto device = requireOpenGl(window);
    test::FakeGlState& gl = window.context.state;

    SECTION("a region with padded rows, BGRA texels into RGBA storage") {
        const TextureHandle texture = *device->createTexture(
            {.format = Format::Bgra8Srgb, .width = 4, .height = 4, .debugName = "Icon"});
        const GLuint name = gl.lastName;
        // 2x2 texels, rows 16 bytes apart: 16 + 8 bytes.
        const std::vector<std::byte> data(24, std::byte{0x7F});
        gl.calls.clear();

        device->writeTexture(texture, {.x = 1, .y = 2, .width = 2, .height = 2}, data, 16);

        const std::vector<std::string> expected{
            "PixelStorei(GL_UNPACK_ALIGNMENT, 1)",
            "PixelStorei(GL_UNPACK_ROW_LENGTH, 4)",
            std::format("TextureSubImage2D({}, 0, 1 2, 2x2, GL_BGRA, GL_UNSIGNED_BYTE, client memory)", name),
        };
        CHECK(gl.calls == expected);
        CHECK(gl.lastPixels == data.data());
    }
    SECTION("layers of an array texture, tightly packed, into a mip level") {
        const TextureHandle texture = *device->createTexture({.dimension = TextureDimension::Tex2DArray,
                                                              .format = Format::R16Float,
                                                              .width = 16,
                                                              .height = 16,
                                                              .depthOrLayers = 3,
                                                              .mipLevels = 2});
        const GLuint name = gl.lastName;
        const std::vector<std::byte> data(usize{8} * 8 * 2 * 2);
        gl.calls.clear();

        device->writeTexture(
            texture, {.mipLevel = 1, .arrayLayer = 1, .layerCount = 2, .width = 8, .height = 8}, data);

        CHECK(gl.calls.at(1) == "PixelStorei(GL_UNPACK_ROW_LENGTH, 0)");
        CHECK(gl.calls.at(2) ==
              std::format("TextureSubImage3D({}, 1, 0 0 1, 8x8x2, GL_RED, GL_HALF_FLOAT, client memory)",
                          name));
    }
}

TEST_CASE("OpenGL backend: generateMipmaps fills the chain from level 0", "[rhi_opengl][texture]") {
    test::FakeGlWindow window;
    const auto device = requireOpenGl(window);
    test::FakeGlState& gl = window.context.state;
    const TextureHandle texture = *device->createTexture(
        {.format = Format::Rgba8Srgb, .width = 32, .height = 32, .mipLevels = kAllMips});
    const GLuint name = gl.lastName;
    gl.calls.clear();

    device->generateMipmaps(texture);

    CHECK(gl.calls == std::vector<std::string>{std::format("GenerateTextureMipmap({})", name)});
}

TEST_CASE("OpenGL backend: copyBufferToTexture reads from a pixel unpack buffer", "[rhi_opengl][texture]") {
    test::FakeGlWindow window;
    const auto device = requireOpenGl(window);
    test::FakeGlState& gl = window.context.state;
    const BufferHandle staging = *device->createBuffer(
        {.sizeInBytes = 1024, .usage = BufferUsage::CopySrc, .memory = MemoryLocation::Upload});
    const GLuint buffer = gl.lastName;
    const TextureHandle texture =
        *device->createTexture({.format = Format::Rgba8Unorm, .width = 8, .height = 8});
    const GLuint name = gl.lastName;
    gl.calls.clear();

    device->beginFrame();
    CommandList& commands = device->acquireCommandList();
    commands.copyBufferToTexture({.buffer = staging,
                                  .bufferOffset = 256,
                                  .bytesPerRow = 64,
                                  .texture = texture,
                                  .width = 8,
                                  .height = 8});
    device->submit(commands);
    device->endFrame();

    const std::vector<std::string> expected{
        std::format("BindBuffer(GL_PIXEL_UNPACK_BUFFER, {})", buffer),
        "PixelStorei(GL_UNPACK_ALIGNMENT, 1)",
        "PixelStorei(GL_UNPACK_ROW_LENGTH, 16)",
        std::format("TextureSubImage2D({}, 0, 0 0, 8x8, GL_RGBA, GL_UNSIGNED_BYTE, buffer {} + 256)", name,
                    buffer),
        "BindBuffer(GL_PIXEL_UNPACK_BUFFER, 0)",
    };
    CHECK(gl.calls == expected);
}

TEST_CASE("OpenGL backend: render passes attach depth-stencil textures and array layers",
          "[rhi_opengl][texture]") {
    test::FakeGlWindow window;
    const auto device = requireOpenGl(window);
    test::FakeGlState& gl = window.context.state;

    SECTION("a color and depth-stencil pass clears both aspects and discards them at the end") {
        const TextureHandle image = device->swapchain()->acquireNextImage();
        const TextureHandle depth = *device->createTexture({.format = Format::Depth32FloatStencil8,
                                                            .width = 320,
                                                            .height = 200,
                                                            .usage = TextureUsage::DepthStencil,
                                                            .debugName = "Depth"});
        const GLuint depthName = gl.lastName;
        gl.calls.clear();

        device->beginFrame();
        CommandList& commands = device->acquireCommandList();
        commands.beginRenderPass(
            {.colorAttachments = {{.texture = image, .loadOp = LoadOp::Clear}},
             .depthStencil = {
                 .texture = depth, .clearDepth = 0.5F, .stencilLoadOp = LoadOp::Clear, .clearStencil = 3}});
        commands.endRenderPass();
        device->submit(commands);
        device->endFrame();

        INFO("calls: " << Catch::rangeToString(gl.calls));
        const GLuint framebuffer = gl.lastName;
        CHECK(called(gl, std::format("NamedFramebufferTexture({}, GL_DEPTH_STENCIL_ATTACHMENT, {}, 0)",
                                     framebuffer, depthName)));
        // The depth mask is closed after device creation; clears need it open.
        const std::vector<std::string> clears = callsFrom(gl, "DepthMask");
        REQUIRE(clears.size() >= 3);
        CHECK(clears[0] == "DepthMask(on)");
        CHECK(clears[1] ==
              std::format("ClearNamedFramebufferfi({}, GL_DEPTH_STENCIL, 0, 0.5, 3)", framebuffer));
        CHECK(called(
            gl, std::format("InvalidateNamedFramebufferData({}, GL_DEPTH_ATTACHMENT GL_STENCIL_ATTACHMENT)",
                            framebuffer)));

        // Destroying the depth texture deletes the framebuffers that use it.
        gl.calls.clear();
        device->destroy(depth);
        CHECK(called(gl, std::format("DeleteFramebuffers({})", framebuffer)));
        CHECK_FALSE(gl.liveFramebuffers.contains(framebuffer));
    }
    SECTION("a depth-only pass takes its render area from the depth texture") {
        const TextureHandle depth = *device->createTexture(
            {.format = Format::Depth32Float, .width = 64, .height = 32, .usage = TextureUsage::DepthStencil});
        const GLuint depthName = gl.lastName;
        gl.calls.clear();

        device->beginFrame();
        CommandList& commands = device->acquireCommandList();
        commands.beginRenderPass({.depthStencil = {.texture = depth,
                                                   .depthLoadOp = LoadOp::Load,
                                                   .depthStoreOp = StoreOp::Store}});
        commands.endRenderPass();
        device->submit(commands);
        device->endFrame();

        INFO("calls: " << Catch::rangeToString(gl.calls));
        const GLuint framebuffer = gl.lastName;
        CHECK(called(gl, std::format("NamedFramebufferTexture({}, GL_DEPTH_ATTACHMENT, {}, 0)", framebuffer,
                                     depthName)));
        CHECK(called(gl, "Viewport(0, 0 0 64 32)"));
        CHECK(std::ranges::none_of(gl.calls, [](const std::string& call) {
            return call.starts_with("ClearNamedFramebuffer") ||
                   call.starts_with("InvalidateNamedFramebufferData");
        }));
    }
    SECTION("a layer of an array texture is attached alone") {
        const TextureHandle layers = *device->createTexture({.dimension = TextureDimension::Tex2DArray,
                                                             .format = Format::Rgba8Unorm,
                                                             .width = 32,
                                                             .height = 32,
                                                             .depthOrLayers = 2,
                                                             .usage = TextureUsage::RenderTarget});
        const GLuint name = gl.lastName;
        gl.calls.clear();

        device->beginFrame();
        CommandList& commands = device->acquireCommandList();
        commands.beginRenderPass({.colorAttachments = {{.texture = layers, .arrayLayer = 1}}});
        commands.endRenderPass();
        device->submit(commands);
        device->endFrame();

        INFO("calls: " << Catch::rangeToString(gl.calls));
        CHECK(called(gl, std::format("NamedFramebufferTextureLayer({}, GL_COLOR_ATTACHMENT0, {}, 0, 1)",
                                     gl.lastName, name)));
    }
}
