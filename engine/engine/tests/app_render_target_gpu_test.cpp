// The scene target and the composite pass on a real OpenGL driver: a 4x2 scene, half sprite and half clear
// color, is scaled 2x into a 10x6 output with a letterbox, and read back. Skipped on machines without a
// window system or an OpenGL 4.5 driver (the CI runners); the null-backend tests cover the same commands.

#include <teserra/engine/app.hpp>
#include <teserra/renderer/assets.hpp>
#include <teserra/renderer/camera.hpp>
#include <teserra/renderer/material.hpp>
#include <teserra/rhi/buffer.hpp>
#include <teserra/rhi/shader_blob.hpp>
#include <teserra/rhi/texture.hpp>
#include <teserra/scene/transform.hpp>

#include <catch2/catch_test_macros.hpp>
#include <engine_tests/shaders.hpp>

#include <array>
#include <cstddef>
#include <cstring>
#include <format>
#include <span>

using namespace teserra;
using namespace teserra::engine;

#if TESERRA_HAS_RHI_OPENGL

namespace {

using Rgba = std::array<u8, 4>;
constexpr Rgba kWhite{255, 255, 255, 255};
constexpr Rgba kRed{255, 0, 0, 255};
constexpr Rgba kBlue{0, 0, 255, 255};
constexpr Rgba kGreen{0, 255, 0, 255};

// A shader asset from an embedded blob, with its reflection.
assets::Asset<renderer::Shader> embeddedShader(App& app, std::string_view path, rhi::ShaderStage stage,
                                               std::span<const std::byte> blob) {
    return app.assets().load<renderer::Shader>(path, [&]() -> Result<renderer::Shader> {
        TESERRA_TRY_ASSIGN(
            const rhi::ShaderModuleHandle module,
            app.device().createShaderModule({.stage = stage, .blob = blob, .debugName = path}));
        TESERRA_TRY_ASSIGN(rhi::ShaderBlobContents contents, rhi::readShaderBlob(blob));
        return renderer::Shader(app.device(), module, stage, std::move(contents.reflection));
    });
}

} // namespace

TEST_CASE("Renderer: a fixed-resolution scene is scaled crisply into the output on the GPU",
          "[engine][renderer][targets][gpu]") {
    App app{{
        .title = "Render target GPU test",
        .windowSize = {64, 64},
        .windowVisible = false,
        .backend = rhi::Backend::OpenGL,
        .sceneTarget = {.resolution = {4, 2}, .letterboxColor = {.r = 0.0F, .g = 0.0F, .b = 1.0F, .a = 1.0F}},
        .maxFrames = 1,
    }};
    if (!app.isInitialized()) {
        REQUIRE(app.initError()->code == ErrorCode::DeviceFailure);
        SKIP("No window system or OpenGL 4.5 driver: " << std::format("{}", *app.initError()));
    }
    rhi::Device& device = app.device();
    constexpr UVec2 kOutputSize{10, 6};
    const rhi::TextureHandle output =
        *device.createTexture({.format = rhi::Format::Rgba8Unorm,
                               .width = kOutputSize.x,
                               .height = kOutputSize.y,
                               .usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::CopySrc,
                               .debugName = "Output"});
    const rhi::BufferHandle readback =
        *device.createBuffer({.sizeInBytes = u64{kOutputSize.x} * kOutputSize.y * 4,
                              .usage = rhi::BufferUsage::CopyDst,
                              .memory = rhi::MemoryLocation::Readback,
                              .debugName = "Readback"});
    const rhi::TextureHandle white =
        *device.createTexture({.format = rhi::Format::Rgba8Unorm, .debugName = "White"});
    device.writeTexture(white, {.width = 1, .height = 1}, std::as_bytes(std::span(kWhite)));

    // A camera 4 units wide and 2 tall on the 4x2 scene; a 2x2 sprite fills its left half; the rest is the
    // red clear color.
    renderer::FramePacket packet;
    renderer::RenderView view = renderer::renderView(
        renderer::Camera{.projection = renderer::Orthographic{.height = 2.0F}}, Mat4{1.0F}, UVec2{4, 2});
    view.clearColor = {.r = 1.0F, .g = 0.0F, .b = 0.0F, .a = 1.0F};
    packet.views.push_back(view);
    packet.sprites.push_back({.world = scene::Transform{.position = {-1.0F, 0.0F, 0.0F}}.matrix(),
                              .size = {2.0F, 2.0F},
                              .texture = white});

    const int exitCode = app.run({.render = [&](App& running, rhi::CommandList& commands) {
        REQUIRE(running.renderer() != nullptr);
        running.renderer()->render(
            packet, {.texture = output, .format = rhi::Format::Rgba8Unorm, .size = kOutputSize}, commands);
        // After the composite: render to texture, then sample it, then copy, with no barrier in the renderer.
        commands.copyTextureToBuffer(
            {.buffer = readback, .texture = output, .width = kOutputSize.x, .height = kOutputSize.y});
    }});
    REQUIRE(exitCode == 0);
    device.waitIdle();

    const auto texel = [&](u32 x, u32 y) {
        Rgba rgba{};
        std::memcpy(rgba.data(), device.map(readback).subspan((usize{y} * kOutputSize.x + x) * 4, 4).data(),
                    rgba.size());
        return rgba;
    };
    // 2x is the largest factor: the 8x4 scene sits at (1, 1); every scene pixel is a sharp 2x2 block.
    CHECK(texel(0, 0) == kBlue);
    CHECK(texel(9, 5) == kBlue);
    CHECK(texel(0, 3) == kBlue);
    CHECK(texel(1, 1) == kWhite);
    CHECK(texel(4, 4) == kWhite); // the last column of the sprite's half
    CHECK(texel(5, 1) == kRed);   // the first column of the clear half: no blending across the edge
    CHECK(texel(8, 4) == kRed);
    CHECK(texel(9, 2) == kBlue);

    device.destroy(white);
    device.destroy(readback);
    device.destroy(output);
}

TEST_CASE("Renderer: a sprite with a custom material draws with its parameters on the GPU",
          "[engine][renderer][material][gpu]") {
    App app{{
        .title = "Material GPU test",
        .windowSize = {64, 64},
        .windowVisible = false,
        .backend = rhi::Backend::OpenGL,
        .maxFrames = 1,
    }};
    if (!app.isInitialized()) {
        REQUIRE(app.initError()->code == ErrorCode::DeviceFailure);
        SKIP("No window system or OpenGL 4.5 driver: " << std::format("{}", *app.initError()));
    }
    rhi::Device& device = app.device();
    constexpr UVec2 kOutputSize{4, 2};
    const rhi::TextureHandle output =
        *device.createTexture({.format = rhi::Format::Rgba8Unorm,
                               .width = kOutputSize.x,
                               .height = kOutputSize.y,
                               .usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::CopySrc,
                               .debugName = "Output"});
    const rhi::BufferHandle readback =
        *device.createBuffer({.sizeInBytes = u64{kOutputSize.x} * kOutputSize.y * 4,
                              .usage = rhi::BufferUsage::CopyDst,
                              .memory = rhi::MemoryLocation::Readback,
                              .debugName = "Readback"});
    const rhi::TextureHandle white =
        *device.createTexture({.format = rhi::Format::Rgba8Unorm, .debugName = "White"});
    device.writeTexture(white, {.width = 1, .height = 1}, std::as_bytes(std::span(kWhite)));

    {
        const assets::Asset<renderer::Material> material =
            app.assets().load<renderer::Material>("game://tint", [&]() -> Result<renderer::Material> {
                return renderer::Material::create(
                    device, {.vertex = embeddedShader(app, "game://tint.vert", rhi::ShaderStage::Vertex,
                                                      test::shaders::kTintVert),
                             .fragment = embeddedShader(app, "game://tint.frag", rhi::ShaderStage::Fragment,
                                                        test::shaders::kTintFrag),
                             .state = {.blend = renderer::BlendMode::Opaque},
                             .debugName = "tint"});
            });
        REQUIRE(material.isReady());
        renderer::MaterialInstance green = *renderer::MaterialInstance::create(device, material);
        REQUIRE(green.set("color", Vec4{0.0F, 1.0F, 0.0F, 1.0F}));

        // The sprite covers the left half of a 4x2 view; the rest is the red clear color.
        renderer::FramePacket packet;
        renderer::RenderView view = renderer::renderView(
            renderer::Camera{.projection = renderer::Orthographic{.height = 2.0F}}, Mat4{1.0F}, kOutputSize);
        view.clearColor = {.r = 1.0F, .g = 0.0F, .b = 0.0F, .a = 1.0F};
        packet.views.push_back(view);
        packet.sprites.push_back({.world = scene::Transform{.position = {-1.0F, 0.0F, 0.0F}}.matrix(),
                                  .size = {2.0F, 2.0F},
                                  .texture = white,
                                  .material = &green});

        const int exitCode = app.run({.render = [&](App& running, rhi::CommandList& commands) {
            REQUIRE(running.renderer() != nullptr);
            running.renderer()->render(
                packet, {.texture = output, .format = rhi::Format::Rgba8Unorm, .size = kOutputSize},
                commands);
            commands.copyTextureToBuffer(
                {.buffer = readback, .texture = output, .width = kOutputSize.x, .height = kOutputSize.y});
        }});
        REQUIRE(exitCode == 0);
        device.waitIdle();
    }

    const auto texel = [&](u32 x, u32 y) {
        Rgba rgba{};
        std::memcpy(rgba.data(), device.map(readback).subspan((usize{y} * kOutputSize.x + x) * 4, 4).data(),
                    rgba.size());
        return rgba;
    };
    CHECK(texel(0, 0) == kGreen); // white texture times the green tint
    CHECK(texel(1, 1) == kGreen);
    CHECK(texel(2, 0) == kRed);
    CHECK(texel(3, 1) == kRed);

    device.destroy(white);
    device.destroy(readback);
    device.destroy(output);
}

#endif
