// Debug draw: shapes become 3D line vertices, the list can be turned off at runtime, and the debug pass
// draws the frame's lines over the scene for every view, creating and allocating nothing after warm-up.

#include <teserra/renderer/debug_draw.hpp>

#include <teserra/renderer/debug_pass.hpp>
#include <teserra/renderer/renderer.hpp>
#include <teserra/rhi/commands.hpp>
#include <teserra/rhi_null/null_device.hpp>
#include <teserra/test/allocation_counter.hpp>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace teserra;
using namespace teserra::renderer;

namespace {

constexpr Color kRed{.r = 1.0F, .g = 0.0F, .b = 0.0F, .a = 1.0F};

struct Scene {
    Scene() {
        renderer = *Renderer::create(*device);
        auto created = DebugPass::create(*device, renderer->frameGroupLayout());
        REQUIRE(created.has_value());
        debug = created->get();
        renderer->addPass(std::move(created).value());
        target = {.texture = *device->createTexture({.format = rhi::Format::Rgba8Srgb,
                                                     .width = 320,
                                                     .height = 200,
                                                     .usage = rhi::TextureUsage::RenderTarget}),
                  .format = rhi::Format::Rgba8Srgb,
                  .size = {320, 200}};
        packet.views.push_back({});
    }
    ~Scene() {
        renderer.reset();
        device->destroy(target.texture);
    }
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&) = delete;
    Scene& operator=(Scene&&) = delete;

    std::string render() {
        device->beginFrame();
        rhi::CommandList& commands = device->acquireCommandList();
        renderer->render(packet, target, commands);
        device->submit(commands);
        std::string described = rhi::describeCommands(*device->submitted()[0]);
        device->endFrame();
        // The debug pass's commands, without the renderer's composite pass after it.
        return described.substr(0, described.find("PushDebugGroup \"Composite\""));
    }

    std::unique_ptr<rhi::null::NullDevice> device =
        rhi::null::createHeadlessDevice({.enableValidation = true});
    std::unique_ptr<Renderer> renderer;
    DebugPass* debug = nullptr;
    FrameTarget target;
    FramePacket packet;
};

usize countOf(const std::string& text, const std::string& word) {
    usize count = 0;
    for (usize at = text.find(word); at != std::string::npos; at = text.find(word, at + word.size())) {
        ++count;
    }
    return count;
}

} // namespace

TEST_CASE("Debug draw: shapes are 3D lines", "[renderer][debug]") {
    if (!kDebugDrawCompiled) {
        SKIP("debug draw is compiled out");
    }
    DebugDrawList list;
    list.line({0.0F, 0.0F, 0.0F}, {1.0F, 2.0F, 3.0F}, kRed);
    REQUIRE(list.vertices().size() == 2);
    CHECK(list.vertices()[1].position == Vec3{1.0F, 2.0F, 3.0F});
    CHECK(list.vertices()[0].color == std::array<u8, 4>{255, 0, 0, 255});

    list.clear();
    list.rect({.position = {1.0F, 1.0F}, .size = {2.0F, 3.0F}}, kRed, 5.0F);
    CHECK(list.lineCount() == 4);
    CHECK(list.vertices()[2].position == Vec3{3.0F, 1.0F, 5.0F}); // second edge starts at the right corner
    CHECK(list.vertices()[4].position == Vec3{3.0F, 4.0F, 5.0F});

    list.clear();
    list.circle({0.0F, 0.0F, 2.0F}, 1.0F, kRed, 16);
    CHECK(list.lineCount() == 16);
    for (const DebugVertex& vertex : list.vertices()) {
        CHECK(vertex.position.z == 2.0F);
    }
    list.cross({0.0F, 0.0F, 0.0F}, 1.0F, kRed);
    CHECK(list.lineCount() == 19);
    list.box({.min = {0.0F, 0.0F, 0.0F}, .max = {1.0F, 1.0F, 1.0F}}, kRed);
    CHECK(list.lineCount() == 31);
}

TEST_CASE("Debug draw: a disabled list ignores shapes", "[renderer][debug]") {
    DebugDrawList list;
    list.setEnabled(false);
    CHECK_FALSE(list.isEnabled());
    list.line({}, {1.0F, 0.0F, 0.0F}, kRed);
    list.circle({}, 1.0F, kRed);
    CHECK(list.vertices().empty());
    list.setEnabled(true);
    list.cross({}, 1.0F, kRed);
    CHECK(list.lineCount() == (kDebugDrawCompiled ? 3U : 0U));
}

TEST_CASE("Debug draw: the debug pass draws the lines of every view over the scene", "[renderer][debug]") {
    if (!kDebugDrawCompiled) {
        SKIP("debug draw is compiled out");
    }
    Scene scene;
    CHECK(countOf(scene.render(), "BeginRenderPass") == 0); // no lines: nothing recorded

    DebugDrawList list;
    list.rect({.position = {0.0F, 0.0F}, .size = {1.0F, 1.0F}}, kRed);
    scene.packet.debugLines.assign(list.vertices().begin(), list.vertices().end());
    scene.packet.views.push_back({.order = 1, .viewport = {.position = {0.5F, 0.0F}, .size = {0.5F, 1.0F}}});

    const std::string commands = scene.render();
    CHECK(countOf(commands, "BeginRenderPass") == 1);
    CHECK(countOf(commands, "load=Load") == 2); // keeps the color and depth that earlier passes drew
    CHECK(countOf(commands, "Draw vertexCount=8") == 2);
    CHECK(scene.debug->stats().lines == 4);
    CHECK(scene.debug->stats().drawCalls == 2);
}

TEST_CASE("Debug draw: steady-state frames create nothing and allocate nothing",
          "[renderer][debug][allocation]") {
    Scene scene;
    DebugDrawList list;
    const auto frame = [&] {
        list.clear();
        for (int i = 0; i < 50; ++i) {
            list.circle({static_cast<f32>(i), 0.0F, 0.0F}, 0.5F, kRed);
        }
        scene.packet.debugLines.assign(list.vertices().begin(), list.vertices().end());
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
