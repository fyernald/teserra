// The sprite pass on the null backend: 10 000 sprites over 16 textures take one draw per texture, with the
// pipeline bound once and the material group bound per texture; blend changes switch pipelines; every view
// sets its area and camera; views render into their camera's target with depth; and a steady-state frame
// creates nothing and allocates nothing.

#include <teserra/renderer/sprite_pass.hpp>

#include <teserra/renderer/renderer.hpp>
#include <teserra/rhi/buffer.hpp>
#include <teserra/rhi_null/null_device.hpp>
#include <teserra/test/allocation_counter.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace teserra;
using namespace teserra::renderer;

namespace {

struct Scene {
    explicit Scene(u32 textureCount) {
        renderer = *Renderer::create(*device);
        auto created = SpritePass::create(*device, renderer->frameGroupLayout());
        REQUIRE(created.has_value());
        sprites = created->get();
        renderer->addPass(std::move(created).value());
        target = {.texture = *device->createTexture({.format = rhi::Format::Rgba8Srgb,
                                                     .width = 320,
                                                     .height = 200,
                                                     .usage = rhi::TextureUsage::RenderTarget}),
                  .format = rhi::Format::Rgba8Srgb,
                  .size = {320, 200}};
        for (u32 i = 0; i < textureCount; ++i) {
            textures.push_back(*device->createTexture({.width = 16, .height = 16}));
        }
        packet.views.push_back({});
    }
    ~Scene() {
        renderer.reset();
        for (const rhi::TextureHandle texture : textures) {
            device->destroy(texture);
        }
        device->destroy(target.texture);
    }
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&) = delete;
    Scene& operator=(Scene&&) = delete;

    // Renders one frame; returns the sprite pass's commands (not the renderer's composite pass after it), one
    // per line without the nesting indentation.
    std::vector<std::string> render() {
        device->beginFrame();
        rhi::CommandList& commands = device->acquireCommandList();
        renderer->render(packet, target, commands);
        device->submit(commands);
        std::istringstream stream(rhi::describeCommands(*device->submitted()[0]));
        device->endFrame();
        std::vector<std::string> lines;
        for (std::string line; std::getline(stream, line);) {
            line = line.substr(line.find_first_not_of(' '));
            if (line == R"(PushDebugGroup "Composite")") {
                break;
            }
            lines.push_back(line);
        }
        return lines;
    }

    std::unique_ptr<rhi::null::NullDevice> device =
        rhi::null::createHeadlessDevice({.enableValidation = true});
    std::unique_ptr<Renderer> renderer;
    SpritePass* sprites = nullptr;
    FrameTarget target;
    std::vector<rhi::TextureHandle> textures;
    FramePacket packet;
};

std::vector<std::string> linesStartingWith(const std::vector<std::string>& lines, std::string_view prefix) {
    std::vector<std::string> kept;
    std::ranges::copy_if(lines, std::back_inserter(kept),
                         [&](const std::string& line) { return line.starts_with(prefix); });
    return kept;
}

usize countStartingWith(const std::vector<std::string>& lines, std::string_view prefix) {
    return linesStartingWith(lines, prefix).size();
}

} // namespace

TEST_CASE("Sprite pass: 10 000 sprites over 16 textures take one draw per texture", "[renderer][sprites]") {
    Scene scene(16);
    scene.packet.sprites.reserve(10'000);
    for (u32 i = 0; i < 10'000; ++i) {
        scene.packet.sprites.push_back({.texture = scene.textures[(i * 7) % 16]});
    }

    const std::vector<std::string> lines = scene.render();

    CHECK(countStartingWith(lines, "DrawIndexed") == 16); // the acceptance limit is 20
    CHECK(countStartingWith(lines, "BindPipeline") == 1);
    CHECK(countStartingWith(lines, "BindGroup index=2") == 16);
    CHECK(countStartingWith(lines, "BindGroup index=0") == 1); // the view's camera
    CHECK(scene.sprites->stats().sprites == 10'000);
    CHECK(scene.sprites->stats().batches == 16);
    CHECK(scene.sprites->stats().drawCalls == 16);
    // Each draw covers its batch's quads from its first vertex: 625 sprites per texture.
    const std::vector<std::string> draws = linesStartingWith(lines, "DrawIndexed");
    REQUIRE(draws.size() == 16);
    CHECK(draws[0] ==
          "DrawIndexed indexCount=3750 instanceCount=1 firstIndex=0 vertexOffset=0 firstInstance=0");
    CHECK(draws[1] ==
          "DrawIndexed indexCount=3750 instanceCount=1 firstIndex=0 vertexOffset=2500 firstInstance=0");
}

TEST_CASE("Sprite pass: a blend change switches the pipeline", "[renderer][sprites]") {
    Scene scene(1);
    scene.packet.sprites = {{.texture = scene.textures[0]}, {.texture = scene.textures[0]}};
    scene.packet.sprites[1].blend = SpriteBlend::Additive;

    const std::vector<std::string> lines = scene.render();
    CHECK(countStartingWith(lines, "DrawIndexed") == 2);
    CHECK(countStartingWith(lines, "BindPipeline") == 2);
    CHECK(countStartingWith(lines, "BindGroup index=2") == 1); // the same texture stays bound
}

TEST_CASE("Sprite pass: every view sets its area and camera", "[renderer][sprites]") {
    Scene scene(1);
    scene.packet.sprites = {{.texture = scene.textures[0]}};
    scene.packet.views = {
        RenderView{.viewport = {.position = {0.0F, 0.0F}, .size = {0.5F, 1.0F}}},
        RenderView{.order = 1, .viewport = {.position = {0.5F, 0.0F}, .size = {0.5F, 1.0F}}}};

    const std::vector<std::string> lines = scene.render();
    CHECK(countStartingWith(lines, "DrawIndexed") == 2);
    CHECK(countStartingWith(lines, "BindGroup index=0") == 2);
    CHECK(linesStartingWith(lines, "SetScissor") ==
          std::vector<std::string>{"SetScissor x=0 y=0 width=160 height=200",
                                   "SetScissor x=160 y=0 width=160 height=200"});
}

TEST_CASE("Sprite pass: no sprites still clears the target", "[renderer][sprites]") {
    Scene scene(0);
    const std::vector<std::string> lines = scene.render();
    CHECK(countStartingWith(lines, "BeginRenderPass") == 1);
    CHECK(countStartingWith(lines, "DrawIndexed") == 0);
}

TEST_CASE("Sprite pass: steady-state frames create nothing and allocate nothing",
          "[renderer][sprites][allocation]") {
    Scene scene(16);
    for (u32 i = 0; i < 1000; ++i) {
        scene.packet.sprites.push_back({.texture = scene.textures[i % 16]});
    }
    const auto frame = [&] {
        scene.device->beginFrame();
        rhi::CommandList& commands = scene.device->acquireCommandList();
        scene.renderer->render(scene.packet, scene.target, commands);
        scene.device->submit(commands);
        scene.device->endFrame();
    };
    for (int warmUp = 0; warmUp < 3; ++warmUp) {
        frame();
    }
    const usize objects = scene.device->liveObjectCount();

    u64 allocations = 0;
    {
        const test::AllocationCounter counter;
        frame();
        frame();
        allocations = counter.count();
    }
    CHECK(allocations == 0);
    CHECK(scene.device->liveObjectCount() == objects);
}

TEST_CASE("Sprite pass: forgetTexture destroys the texture's bind group", "[renderer][sprites]") {
    Scene scene(1);
    scene.packet.sprites = {{.texture = scene.textures[0]}};
    static_cast<void>(scene.render());
    const usize withGroup = scene.device->liveObjectCount();

    scene.sprites->forgetTexture(scene.textures[0]);
    CHECK(scene.device->liveObjectCount() == withGroup - 1);
    scene.sprites->forgetTexture(scene.textures[0]); // nothing left to forget
    CHECK(scene.device->liveObjectCount() == withGroup - 1);
}

TEST_CASE("Sprite pass: tilemap chunks draw before the sprites of their layer",
          "[renderer][sprites][tilemap]") {
    Scene scene(2);
    const rhi::TextureHandle tiles = scene.textures[0];
    const rhi::TextureHandle hero = scene.textures[1];
    std::vector<rhi::BufferHandle> buffers;
    buffers.reserve(3);
    for (int i = 0; i < 3; ++i) {
        buffers.push_back(*scene.device->createBuffer(
            {.sizeInBytes = 4096, .usage = rhi::BufferUsage::Vertex | rhi::BufferUsage::CopyDst}));
    }
    // Chunks given out of layer order; one has no quads and is skipped.
    scene.packet.tilemapChunks = {
        {.vertexBuffer = buffers[0], .quadCount = 3, .texture = tiles, .layer = 2},
        {.vertexBuffer = buffers[1], .quadCount = 5, .texture = tiles, .layer = 0},
        {.vertexBuffer = buffers[2], .quadCount = 0, .texture = tiles, .layer = 1},
    };
    // Sprites on layers 0, 1 and 3, all of one texture: one batch that the chunk of layer 2 splits.
    scene.packet.sprites = {
        {.texture = hero, .layer = 0}, {.texture = hero, .layer = 1}, {.texture = hero, .layer = 3}};

    const std::vector<std::string> draws = linesStartingWith(scene.render(), "DrawIndexed");
    // Layer 0 chunk, sprites of layers 0 and 1, layer 2 chunk, sprite of layer 3.
    CHECK(draws ==
          std::vector<std::string>{
              "DrawIndexed indexCount=30 instanceCount=1 firstIndex=0 vertexOffset=0 firstInstance=0",
              "DrawIndexed indexCount=12 instanceCount=1 firstIndex=0 vertexOffset=0 firstInstance=0",
              "DrawIndexed indexCount=18 instanceCount=1 firstIndex=0 vertexOffset=0 firstInstance=0",
              "DrawIndexed indexCount=6 instanceCount=1 firstIndex=0 vertexOffset=8 firstInstance=0",
          });
    CHECK(scene.sprites->stats().tilemapChunks == 2);
    CHECK(scene.sprites->stats().batches == 1);
    CHECK(scene.sprites->stats().drawCalls == 4);
    for (const rhi::BufferHandle buffer : buffers) {
        scene.device->destroy(buffer);
    }
}

TEST_CASE("Sprite pass: tilemap chunks draw without sprites, with their own sampler",
          "[renderer][sprites][tilemap]") {
    Scene scene(1);
    const rhi::BufferHandle buffer = *scene.device->createBuffer(
        {.sizeInBytes = 4096, .usage = rhi::BufferUsage::Vertex | rhi::BufferUsage::CopyDst});
    scene.packet.tilemapChunks = {{.vertexBuffer = buffer, .quadCount = 1, .texture = scene.textures[0]},
                                  {.vertexBuffer = buffer, .quadCount = 2, .texture = scene.textures[0]}};
    const std::vector<std::string> lines = scene.render();
    CHECK(countStartingWith(lines, "DrawIndexed") == 2);
    CHECK(countStartingWith(lines, "BindVertexBuffer") == 1); // the same buffer stays bound
    CHECK(countStartingWith(lines, "BindGroup index=2") == 1);
    const usize objects = scene.device->liveObjectCount();

    // The same texture on a sprite gets a second group, with the sprite sampler.
    scene.packet.sprites = {{.texture = scene.textures[0], .layer = 5}};
    const std::vector<std::string> mixed = scene.render();
    CHECK(countStartingWith(mixed, "BindGroup index=2") == 2);
    CHECK(scene.device->liveObjectCount() == objects + 1);
    scene.sprites->forgetTexture(scene.textures[0]); // both groups
    CHECK(scene.device->liveObjectCount() == objects - 1);
    scene.device->destroy(buffer);
}

TEST_CASE("Sprite pass: the scene target has a depth attachment, cleared with the color",
          "[renderer][sprites][targets]") {
    Scene scene(1);
    scene.packet.sprites = {{.texture = scene.textures[0]}};
    const std::vector<std::string> passes = linesStartingWith(scene.render(), "BeginRenderPass");
    REQUIRE(passes.size() == 1);
    CHECK(passes[0].find("load=Clear store=Store clear=(0, 0, 0, 1) depth=") != std::string::npos);
    CHECK(passes[0].ends_with("load=Clear store=Store clear=1"));
}

TEST_CASE("Sprite pass: views render into their camera's target, each target cleared once",
          "[renderer][sprites][targets]") {
    Scene scene(1);
    const RenderTargetHandle minimap =
        *scene.renderer->createRenderTarget({.size = {64, 48}, .debugName = "Map"});
    const rhi::TextureHandle minimapColor = scene.renderer->renderTarget(minimap)->texture;
    scene.packet.sprites = {{.texture = scene.textures[0]}};
    scene.packet.views = {
        RenderView{.order = 1},
        RenderView{.clearColor = {.r = 1.0F, .g = 0.0F, .b = 0.0F, .a = 1.0F}, .order = 0, .target = minimap},
        RenderView{.order = 2, .viewport = {.position = {0.5F, 0.0F}, .size = {0.5F, 0.5F}}},
    };

    const std::vector<std::string> lines = scene.render();
    const std::vector<std::string> passes = linesStartingWith(lines, "BeginRenderPass");
    // The minimap first (order 0), then both scene views in one pass.
    REQUIRE(passes.size() == 2);
    CHECK(passes[0].starts_with(
        std::format("BeginRenderPass color0={} load=Clear store=Store clear=(1, 0, 0, 1)", minimapColor)));
    CHECK(passes[1].find("load=Clear") != std::string::npos);
    CHECK(linesStartingWith(lines, "SetScissor") ==
          std::vector<std::string>{"SetScissor x=0 y=0 width=64 height=48",
                                   "SetScissor x=0 y=0 width=320 height=200",
                                   "SetScissor x=160 y=0 width=160 height=100"});
    CHECK(countStartingWith(lines, "DrawIndexed") == 3);
    scene.renderer->destroyRenderTarget(minimap);
}

TEST_CASE("Sprite pass: the scene target is cleared when every view has its own target",
          "[renderer][sprites][targets]") {
    Scene scene(0);
    const RenderTargetHandle minimap = *scene.renderer->createRenderTarget({.size = {32, 32}});
    scene.packet.views = {RenderView{.target = minimap}};
    const std::vector<std::string> passes = linesStartingWith(scene.render(), "BeginRenderPass");
    REQUIRE(passes.size() == 2);
    CHECK(passes[0].starts_with(
        std::format("BeginRenderPass color0={}", scene.renderer->renderTarget(minimap)->texture)));
    CHECK(passes[1].find("load=Clear") != std::string::npos);
    scene.renderer->destroyRenderTarget(minimap);
}

TEST_CASE("Sprite pass: a sprite showing a target is not drawn into that target",
          "[renderer][sprites][targets]") {
    Scene scene(1);
    const RenderTargetHandle minimap = *scene.renderer->createRenderTarget({.size = {32, 32}});
    const rhi::TextureHandle minimapColor = scene.renderer->renderTarget(minimap)->texture;
    scene.packet.sprites = {{.texture = scene.textures[0]}, {.texture = minimapColor, .layer = 1}};
    scene.packet.views = {RenderView{.target = minimap}, RenderView{.order = 1}};

    const std::vector<std::string> lines = scene.render();
    // The minimap's view draws only the other sprite; the scene's view draws both.
    CHECK(countStartingWith(lines, "DrawIndexed") == 3);
    CHECK(scene.sprites->stats().drawCalls == 3);
    scene.sprites->forgetTexture(minimapColor);
    scene.renderer->destroyRenderTarget(minimap);
}
