// The five snapshots of #111, rendered by the real backend at 160x90 and compared with tests/golden/*.png:
// a clear, a vertex-colored triangle, a textured quad, sprites with blending and layers, and a tilemap.
// Scenes keep to pixel-aligned, nearest-sampled geometry, so a software rasterizer (llvmpipe in CI) and a
// hardware GPU agree within the default tolerance; the triangle's edges and interpolation get more room.

#include "snapshot.hpp"

#include <teserra/renderer/assets.hpp>
#include <teserra/renderer/camera.hpp>
#include <teserra/renderer/chunk_mesh_cache.hpp>
#include <teserra/renderer/frame_packet.hpp>
#include <teserra/renderer/renderer.hpp>
#include <teserra/rhi/buffer.hpp>
#include <teserra/rhi/pipeline.hpp>
#include <teserra/rhi/shader.hpp>
#include <teserra/rhi/texture.hpp>
#include <teserra/scene/render_components.hpp>
#include <teserra/scene/render_extraction.hpp>
#include <teserra/scene/scene.hpp>
#include <teserra/scene/tilemap.hpp>
#include <teserra/scene/tileset.hpp>
#include <teserra/scene/transform.hpp>

#include <catch2/catch_test_macros.hpp>
#include <snapshot_tests/shaders.hpp>

#include <array>
#include <cstddef>
#include <initializer_list>
#include <span>
#include <vector>

using namespace teserra;
using namespace teserra::snapshot;

namespace {

// A camera at the origin showing `pixelsPerUnit` output pixels per world unit.
renderer::RenderView view(f32 pixelsPerUnit, Color clear) {
    renderer::RenderView result = renderer::renderView(
        renderer::Camera{.projection = renderer::Orthographic{.height = static_cast<f32>(kSnapshotSize.y) /
                                                                        pixelsPerUnit}},
        Mat4{1.0F}, kSnapshotSize);
    result.clearColor = clear;
    return result;
}

// An Rgba8Unorm texture of `width` x `height` texels, rows top first.
rhi::TextureHandle texture(rhi::Device& device, u32 width, u32 height, const std::vector<u8>& rgba) {
    const rhi::TextureHandle handle =
        *device.createTexture({.format = rhi::Format::Rgba8Unorm, .width = width, .height = height});
    device.writeTexture(handle, {.width = width, .height = height}, std::as_bytes(std::span(rgba)));
    return handle;
}

Mat4 at(f32 x, f32 y) {
    return scene::Transform{.position = {x, y, 0.0F}}.matrix();
}

} // namespace

TEST_CASE("Snapshot: clear", "[snapshot][gpu]") {
    SnapshotRenderer snapshot;
    const Image image = snapshot.render([&](rhi::CommandList& commands) {
        commands.beginRenderPass({.colorAttachments = {{.texture = snapshot.output().texture,
                                                        .loadOp = rhi::LoadOp::Clear,
                                                        .clearColor = {0.2F, 0.4F, 0.8F, 1.0F}}}});
        commands.endRenderPass();
    });
    checkSnapshot("clear", image);
}

TEST_CASE("Snapshot: vertex-colored triangle", "[snapshot][gpu]") {
    SnapshotRenderer snapshot;
    rhi::Device& device = snapshot.device();
    const rhi::ShaderModuleHandle vertex =
        *device.createShaderModule({.stage = rhi::ShaderStage::Vertex,
                                    .blob = test::shaders::kVertexColorVert,
                                    .debugName = "Triangle vertex"});
    const rhi::ShaderModuleHandle fragment =
        *device.createShaderModule({.stage = rhi::ShaderStage::Fragment,
                                    .blob = test::shaders::kVertexColorFrag,
                                    .debugName = "Triangle fragment"});
    constexpr std::array kBuffers{rhi::VertexBufferLayout{.stride = 28}};
    constexpr std::array kAttributes{
        rhi::VertexAttribute{.location = 0, .format = rhi::VertexFormat::Float3},
        rhi::VertexAttribute{.location = 1, .format = rhi::VertexFormat::Float4, .offset = 12},
    };
    const rhi::GraphicsPipelineHandle pipeline = *device.createGraphicsPipeline({
        .vertexShader = vertex,
        .fragmentShader = fragment,
        .vertexBuffers = kBuffers,
        .vertexAttributes = kAttributes,
        .targets = {.colorFormats = {rhi::Format::Rgba8Unorm}},
        .debugName = "Triangle",
    });
    // Normalized device coordinates, +Y up: red at the top, green bottom left, blue bottom right.
    constexpr std::array<f32, 21> kVertices{
        0.0F,  0.8F,  0.0F, 1.0F, 0.0F, 0.0F, 1.0F, //
        -0.8F, -0.8F, 0.0F, 0.0F, 1.0F, 0.0F, 1.0F, //
        0.8F,  -0.8F, 0.0F, 0.0F, 0.0F, 1.0F, 1.0F, //
    };
    const rhi::BufferHandle vertices = *device.createBuffer(
        {.sizeInBytes = sizeof(kVertices), .usage = rhi::BufferUsage::Vertex | rhi::BufferUsage::CopyDst});
    device.writeBuffer(vertices, 0, std::as_bytes(std::span(kVertices)));

    const Image image = snapshot.render([&](rhi::CommandList& commands) {
        commands.beginRenderPass({.colorAttachments = {{.texture = snapshot.output().texture,
                                                        .loadOp = rhi::LoadOp::Clear,
                                                        .clearColor = {0.0F, 0.0F, 0.0F, 1.0F}}}});
        commands.setViewport(
            {.width = static_cast<f32>(kSnapshotSize.x), .height = static_cast<f32>(kSnapshotSize.y)});
        commands.setScissor({.width = kSnapshotSize.x, .height = kSnapshotSize.y});
        commands.bindPipeline(pipeline);
        commands.bindVertexBuffer(0, vertices, 0);
        commands.draw({.vertexCount = 3});
        commands.endRenderPass();
    });
    // Rasterizers may differ by one step in interpolation and in a few edge pixels.
    checkSnapshot("triangle", image, {.channel = 3, .maxDifferingRatio = 0.02});

    device.destroy(vertices);
    device.destroy(pipeline);
    device.destroy(fragment);
    device.destroy(vertex);
}

TEST_CASE("Snapshot: textured quad", "[snapshot][gpu]") {
    SnapshotRenderer snapshot;
    rhi::Device& device = snapshot.device();
    // 4x4 texels in four colors, a different one in the top-left corner (texel row 0 is the top).
    std::vector<u8> texels;
    for (u32 y = 0; y < 4; ++y) {
        for (u32 x = 0; x < 4; ++x) {
            const bool corner = x == 0 && y == 0;
            const bool even = (x + y) % 2 == 0;
            texels.insert(texels.end(), corner ? std::initializer_list<u8>{255, 40, 40, 255}
                                        : even ? std::initializer_list<u8>{240, 240, 240, 255}
                                               : std::initializer_list<u8>{30, 90, 200, 255});
        }
    }
    const rhi::TextureHandle checker = texture(device, 4, 4, texels);
    renderer::FramePacket packet;
    packet.views.push_back(view(10.0F, {.r = 0.1F, .g = 0.1F, .b = 0.12F, .a = 1.0F}));
    // 6 units = 60 pixels: each texel is a 15x15 block.
    packet.sprites.push_back({.world = at(0.0F, 0.0F), .size = {6.0F, 6.0F}, .texture = checker});

    const Image image = snapshot.render([&](rhi::CommandList& commands) {
        snapshot.app().renderer()->render(packet, snapshot.output(), commands);
    });
    checkSnapshot("textured_quad", image);
    device.destroy(checker);
}

TEST_CASE("Snapshot: sprites with blending and layers", "[snapshot][gpu]") {
    SnapshotRenderer snapshot;
    rhi::Device& device = snapshot.device();
    const rhi::TextureHandle white = texture(device, 1, 1, {255, 255, 255, 255});
    renderer::FramePacket packet;
    packet.views.push_back(view(10.0F, {.r = 0.05F, .g = 0.05F, .b = 0.08F, .a = 1.0F}));
    // Given out of draw order: layers decide.
    packet.sprites = {
        {.world = at(-3.0F, 0.0F),
         .size = {2.0F, 6.0F},
         .tint = {.r = 0.2F, .g = 1.0F, .b = 0.3F, .a = 0.5F},
         .texture = white,
         .layer = 1}, // translucent green over the red square
        {.world = at(-4.0F, 0.0F),
         .size = {4.0F, 4.0F},
         .tint = {.r = 1.0F, .g = 0.2F, .b = 0.2F, .a = 1.0F},
         .texture = white,
         .layer = 0},
        {.world = at(4.0F, 0.0F),
         .size = {4.0F, 4.0F},
         .tint = {.r = 0.3F, .g = 0.3F, .b = 0.9F, .a = 1.0F},
         .texture = white,
         .layer = 0},
        {.world = at(5.0F, 1.0F),
         .size = {3.0F, 3.0F},
         .tint = {.r = 0.6F, .g = 0.5F, .b = 0.0F, .a = 1.0F},
         .texture = white,
         .blend = renderer::SpriteBlend::Additive,
         .layer = 2}, // brightens the blue square
        {.world = at(0.0F, -3.0F),
         .size = {10.0F, 1.0F},
         .tint = {.r = 0.9F, .g = 0.9F, .b = 0.9F, .a = 1.0F},
         .texture = white,
         .layer = -1}, // under everything
    };

    const Image image = snapshot.render([&](rhi::CommandList& commands) {
        snapshot.app().renderer()->render(packet, snapshot.output(), commands);
    });
    checkSnapshot("sprites", image);
    device.destroy(white);
}

TEST_CASE("Snapshot: tilemap", "[snapshot][gpu]") {
    SnapshotRenderer snapshot;
    engine::App& app = snapshot.app();
    // Two 8x8 tiles side by side: a two-tone checker and a framed block.
    std::vector<u8> texels;
    for (u32 y = 0; y < 8; ++y) {
        for (u32 x = 0; x < 16; ++x) {
            if (x < 8) {
                const bool light = ((x / 4) + (y / 4)) % 2 == 0;
                texels.insert(texels.end(), light ? std::initializer_list<u8>{120, 110, 150, 255}
                                                  : std::initializer_list<u8>{70, 62, 90, 255});
            } else {
                const bool frame = x == 8 || x == 15 || y == 0 || y == 7;
                texels.insert(texels.end(), frame ? std::initializer_list<u8>{40, 30, 20, 255}
                                                  : std::initializer_list<u8>{230, 180, 60, 255});
            }
        }
    }
    const assets::Asset<renderer::Texture> atlas =
        app.assets().load<renderer::Texture>("snapshot://tiles", [&] {
            return renderer::Texture::create(app.device(), {16, 8}, std::as_bytes(std::span(texels)),
                                             rhi::Format::Rgba8Unorm, "Tiles");
        });
    REQUIRE(atlas.isReady());
    const scene::Tileset tileset = *scene::Tileset::grid(atlas, {.cellSize = {8, 8}, .count = 2});
    renderer::ChunkMeshCache chunkMeshes(app.device());
    {
        scene::Scene level("tilemap");
        // 8 pixels per unit, so each 8x8 tile covers exactly 8x8 pixels.
        const ecs::Entity camera = level.spawn();
        level.add<scene::Transform>(camera);
        level.add<scene::Camera>(camera,
                                 scene::Camera{.projection = scene::Orthographic{.height = 90.0F / 8.0F},
                                               .clearColor = {.r = 0.0F, .g = 0.0F, .b = 0.0F, .a = 1.0F}});
        scene::Tilemap map;
        scene::TilemapLayer& ground = map.addLayer("ground");
        for (int y = 0; y < 10; ++y) {
            for (int x = 0; x < 20; ++x) {
                if ((x * 3 + y * 5) % 7 != 0) { // leave some holes
                    ground.setTile({x, y}, (x + y) % 3 == 0 ? 2 : 1);
                }
            }
        }
        const ecs::Entity tiles = level.spawn();
        level.add<scene::Transform>(tiles, scene::Transform{.position = {-10.0F, -5.0F, 0.0F}});
        level.add<scene::Tilemap>(tiles, std::move(map));
        level.add<scene::TilemapRenderer>(tiles, scene::TilemapRenderer{.tileset = &tileset});

        auto& extraction = level.world().resource<scene::RenderExtraction>();
        extraction.targetSize = kSnapshotSize;
        extraction.chunkMeshes = &chunkMeshes;
        level.run(ecs::Phase::PostUpdate);
        level.run(ecs::Phase::Extract);
        REQUIRE(extraction.packet.tilemapChunks.size() == 1);

        const Image image = snapshot.render([&](rhi::CommandList& commands) {
            app.renderer()->render(extraction.packet, snapshot.output(), commands);
        });
        checkSnapshot("tilemap", image);
    }
    app.device().waitIdle();
}
