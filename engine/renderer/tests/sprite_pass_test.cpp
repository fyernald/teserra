// The sprite pass on the null backend: 10 000 sprites over 16 textures take one draw per texture, with the
// pipeline bound once and the material group bound per texture; blend changes switch pipelines; every view
// sets its area and camera; and a steady-state frame creates nothing and allocates nothing.

#include <teserra/renderer/sprite_pass.hpp>

#include <teserra/renderer/renderer.hpp>
#include <teserra/rhi_null/null_device.hpp>
#include <teserra/test/allocation_counter.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
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

    // Renders one frame; returns its commands, one per line without the nesting indentation.
    std::vector<std::string> render() {
        device->beginFrame();
        rhi::CommandList& commands = device->acquireCommandList();
        renderer->render(packet, target, commands);
        device->submit(commands);
        std::istringstream stream(rhi::describeCommands(*device->submitted()[0]));
        device->endFrame();
        std::vector<std::string> lines;
        for (std::string line; std::getline(stream, line);) {
            lines.push_back(line.substr(line.find_first_not_of(' ')));
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
