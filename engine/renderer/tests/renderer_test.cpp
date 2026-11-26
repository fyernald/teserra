// The renderer's frame structure on the null backend: passes run in the order they were added, each inside
// a debug group, and the composite pass last; they reach only the resources they declared; every view's
// camera is uploaded at an aligned offset of the frame's ring partition and bound at set 0 with that offset;
// the scene target (color and depth) follows the output or a fixed resolution, and is recreated on resize
// without leaks; camera targets are created and destroyed; the composite pass scales by whole factors with
// a letterbox; overlay passes run after the composite, or alone with renderOverlays(); and a steady-state
// frame allocates nothing.

#include <teserra/renderer/renderer.hpp>

#include <teserra/rhi_null/null_device.hpp>
#include <teserra/test/allocation_counter.hpp>
#include <teserra/test/assert.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstring>
#include <format>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace teserra;
using namespace teserra::renderer;

namespace {

// A pass made of two functions, for tests.
class TestPass final : public RenderPass {
public:
    TestPass(std::string_view name, std::function<void(PassBuilder&)> declare,
             std::function<void(PassContext&, rhi::CommandList&)> execute)
        : m_name(name), m_declare(std::move(declare)), m_execute(std::move(execute)) {}

    [[nodiscard]] std::string_view name() const override { return m_name; }
    void declare(PassBuilder& builder) override { m_declare(builder); }
    void execute(PassContext& context, rhi::CommandList& commands) override { m_execute(context, commands); }

private:
    std::string m_name;
    std::function<void(PassBuilder&)> m_declare;
    std::function<void(PassContext&, rhi::CommandList&)> m_execute;
};

// Clears SceneColor and binds every view, as a scene pass does.
std::unique_ptr<RenderPass> viewsPass(std::string_view name) {
    return std::make_unique<TestPass>(
        name, [](PassBuilder& builder) { builder.writes(PassResource::SceneColor); },
        [](PassContext& context, rhi::CommandList& commands) {
            commands.beginRenderPass({.colorAttachments = {{
                                          .texture = context.target(PassResource::SceneColor).texture,
                                          .loadOp = rhi::LoadOp::Clear,
                                      }}});
            for (usize view = 0; view < context.views().size(); ++view) {
                context.bindView(commands, view);
            }
            commands.endRenderPass();
        });
}

// A renderer on a headless null device with an output texture, as a swapchain image would be.
struct Scene {
    explicit Scene(const SceneTargetConfig& sceneTarget = {}, UVec2 outputSize = {64, 32}) {
        renderer = *Renderer::create(*device, {.uploadBytesPerFrame = 4096, .scene = sceneTarget});
        target = output(outputSize);
    }
    ~Scene() {
        renderer.reset();
        for (const rhi::TextureHandle texture : outputs) {
            device->destroy(texture);
        }
    }
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&) = delete;
    Scene& operator=(Scene&&) = delete;

    // A new output texture of `size`.
    FrameTarget output(UVec2 size) {
        const rhi::TextureHandle texture = *device->createTexture({.format = rhi::Format::Rgba8Srgb,
                                                                   .width = size.x,
                                                                   .height = size.y,
                                                                   .usage = rhi::TextureUsage::RenderTarget,
                                                                   .debugName = "Output"});
        outputs.push_back(texture);
        return {.texture = texture, .format = rhi::Format::Rgba8Srgb, .size = size};
    }

    // Renders one frame and returns its commands as describeCommands() lines; with `overlaysOnly`, through
    // renderOverlays().
    std::vector<std::string> render(const FramePacket& packet, bool overlaysOnly = false) {
        device->beginFrame();
        rhi::CommandList& commands = device->acquireCommandList();
        if (overlaysOnly) {
            renderer->renderOverlays(target, commands);
        } else {
            renderer->render(packet, target, commands);
        }
        device->submit(commands);
        const std::string described = rhi::describeCommands(*device->submitted()[0]);
        device->endFrame();
        std::vector<std::string> lines;
        std::istringstream stream(described);
        for (std::string line; std::getline(stream, line);) {
            lines.push_back(line.substr(line.find_first_not_of(' '))); // without the nesting indentation
        }
        return lines;
    }

    std::unique_ptr<rhi::null::NullDevice> device =
        rhi::null::createHeadlessDevice({.enableValidation = true});
    std::unique_ptr<Renderer> renderer;
    FrameTarget target;
    std::vector<rhi::TextureHandle> outputs;
};

// The commands of the composite pass in `lines`, without its debug group.
std::vector<std::string> compositeLines(const std::vector<std::string>& lines) {
    const auto begin = std::ranges::find(lines, R"(PushDebugGroup "Composite")");
    REQUIRE(begin != lines.end());
    return {begin + 1, lines.end() - 1};
}

std::vector<std::string> linesStartingWith(const std::vector<std::string>& lines,
                                           std::initializer_list<std::string_view> prefixes) {
    std::vector<std::string> kept;
    for (const std::string& line : lines) {
        for (const std::string_view prefix : prefixes) {
            if (line.starts_with(prefix)) {
                kept.push_back(line);
                break;
            }
        }
    }
    return kept;
}

RenderView viewWithTime(f32 time, i32 order) {
    RenderView view;
    view.camera.time = time;
    view.order = order;
    return view;
}

} // namespace

TEST_CASE("Renderer: passes run in the order they were added, each in a debug group, then the composite",
          "[renderer]") {
    Scene scene;
    std::vector<std::string> ran;
    for (const std::string_view name : {"Scene2D", "Debug", "Overlay"}) {
        scene.renderer->addPass(std::make_unique<TestPass>(
            name, [](PassBuilder& /*builder*/) {},
            [&ran, name](PassContext& /*context*/, rhi::CommandList& /*commands*/) {
                ran.emplace_back(name);
            }));
    }

    const std::vector<std::string> lines = scene.render({});

    CHECK(ran == std::vector<std::string>{"Scene2D", "Debug", "Overlay"});
    REQUIRE(lines.size() > 7);
    CHECK(std::vector(lines.begin(), lines.begin() + 7) == std::vector<std::string>{
                                                               "PushDebugGroup \"Scene2D\"",
                                                               "PopDebugGroup",
                                                               "PushDebugGroup \"Debug\"",
                                                               "PopDebugGroup",
                                                               "PushDebugGroup \"Overlay\"",
                                                               "PopDebugGroup",
                                                               "PushDebugGroup \"Composite\"",
                                                           });
    CHECK(lines.back() == "PopDebugGroup");
}

// An overlay pass that records its name and the output it was given, and uses the upload ring.
std::unique_ptr<RenderPass> overlayPass(std::string_view name, std::vector<std::string>& ran,
                                        rhi::TextureHandle& output) {
    return std::make_unique<TestPass>(
        name, [](PassBuilder& builder) { builder.writes(PassResource::Output); },
        [&ran, &output, name](PassContext& context, rhi::CommandList& /*commands*/) {
            ran.emplace_back(name);
            output = context.target(PassResource::Output).texture;
            CHECK(context.uploads().allocate(64, 16).has_value());
            CHECK(context.views().empty());
        });
}

TEST_CASE("Renderer: overlay passes run after the composite, in the order they were added", "[renderer]") {
    Scene scene;
    std::vector<std::string> ran;
    rhi::TextureHandle output;
    scene.renderer->addPass(viewsPass("Scene2D"));
    scene.renderer->addOverlayPass(overlayPass("First overlay", ran, output));
    scene.renderer->addOverlayPass(overlayPass("Second overlay", ran, output));

    const std::vector<std::string> groups = linesStartingWith(scene.render({}), {"PushDebugGroup"});

    CHECK(groups == std::vector<std::string>{R"(PushDebugGroup "Scene2D")", R"(PushDebugGroup "Composite")",
                                             R"(PushDebugGroup "First overlay")",
                                             R"(PushDebugGroup "Second overlay")"});
    CHECK(ran == std::vector<std::string>{"First overlay", "Second overlay"});
    CHECK(output == scene.target.texture);
}

TEST_CASE("Renderer: renderOverlays() runs only the overlay passes, with the frame's upload memory",
          "[renderer]") {
    Scene scene;
    std::vector<std::string> ran;
    rhi::TextureHandle output;
    bool scenePassRan = false;
    scene.renderer->addPass(std::make_unique<TestPass>(
        "Scene2D", [](PassBuilder& builder) { builder.writes(PassResource::SceneColor); },
        [&](PassContext& /*context*/, rhi::CommandList& /*commands*/) { scenePassRan = true; }));
    scene.renderer->addOverlayPass(overlayPass("Overlay", ran, output));

    // Several frames, so the ring's partitions are each started once per frame (allocate() asserts
    // otherwise).
    for (int frame = 0; frame < 4; ++frame) {
        const std::vector<std::string> groups =
            linesStartingWith(scene.render({}, /*overlaysOnly=*/true), {"PushDebugGroup"});
        CHECK(groups == std::vector<std::string>{R"(PushDebugGroup "Overlay")"});
    }

    CHECK(ran.size() == 4);
    CHECK_FALSE(scenePassRan);
    CHECK(output == scene.target.texture);
}

TEST_CASE("Renderer: views are sorted by order and their cameras bound at set 0", "[renderer]") {
    Scene scene;
    scene.renderer->addPass(viewsPass("Views"));
    std::vector<f32> seenTimes;
    rhi::BufferHandle ring;
    scene.renderer->addPass(std::make_unique<TestPass>(
        "Inspect", [](PassBuilder& /*builder*/) {},
        [&](PassContext& context, rhi::CommandList& /*commands*/) {
            ring = context.uploads().buffer();
            for (const RenderView& view : context.views()) {
                seenTimes.push_back(view.camera.time);
            }
        }));
    FramePacket packet;
    packet.views = {viewWithTime(1.0F, 2), viewWithTime(2.0F, -1), viewWithTime(3.0F, 2)};

    const std::vector<std::string> binds = linesStartingWith(scene.render(packet), {"BindGroup index=0"});

    CHECK(seenTimes == std::vector<f32>{2.0F, 1.0F, 3.0F}); // ascending order, ties in packet order
    // One camera block per view, each at the next multiple of the device's uniform offset alignment: 288
    // bytes take 512 on the null device, whose alignment is 256. All in slot 0's partition.
    const u32 alignment = scene.device->limits().uniformBufferOffsetAlignment;
    const u32 stride = (static_cast<u32>(sizeof(CameraData)) + alignment - 1) / alignment * alignment;
    CHECK(stride == 512);
    REQUIRE(binds.size() == 3);
    for (u32 view = 0; view < 3; ++view) {
        INFO("view " << view);
        CHECK(binds[view].starts_with("BindGroup index=0 group="));
        CHECK(binds[view].ends_with(std::format("dynamicOffsets=[{}]", view * stride)));
        CameraData uploaded{};
        std::memcpy(&uploaded, scene.device->bufferData(ring).subspan(usize{view} * stride).data(),
                    sizeof(uploaded));
        CHECK(uploaded.time == seenTimes[view]);
    }

    // The next frame writes the other slot's partition.
    const std::vector<std::string> next = linesStartingWith(scene.render(packet), {"BindGroup index=0"});
    REQUIRE(next.size() == 3);
    CHECK(next[0].ends_with("dynamicOffsets=[4096]"));
}

TEST_CASE("Renderer: passes get the targets they declared", "[renderer][targets]") {
    Scene scene;
    FrameTarget seenScene;
    FrameTarget seenOutput;
    FrameTarget seenView;
    scene.renderer->addPass(std::make_unique<TestPass>(
        "Reader",
        [](PassBuilder& builder) {
            builder.reads(PassResource::SceneColor);
            builder.reads(PassResource::Output);
        },
        [&](PassContext& context, rhi::CommandList& /*commands*/) {
            seenScene = context.target(PassResource::SceneColor);
            seenOutput = context.target(PassResource::Output);
            seenView = context.viewTarget(0);
        }));
    FramePacket packet;
    packet.views.emplace_back();

    static_cast<void>(scene.render(packet));
    // The scene target is the renderer's own, of the output's size and format, with depth.
    CHECK(seenScene.texture != scene.target.texture);
    CHECK(seenScene.size == UVec2{64, 32});
    CHECK(seenScene.format == rhi::Format::Rgba8Srgb);
    CHECK(seenScene.depthFormat == rhi::Format::Depth32Float);
    REQUIRE(scene.device->texture(seenScene.depthTexture) != nullptr);
    CHECK(scene.device->texture(seenScene.depthTexture)->format == rhi::Format::Depth32Float);
    CHECK(scene.device->texture(seenScene.texture)->usage ==
          (rhi::TextureUsage::RenderTarget | rhi::TextureUsage::Sampled));
    CHECK(seenOutput.texture == scene.target.texture);
    CHECK(seenView.texture == seenScene.texture); // a view without a target renders into the scene target
}

TEST_CASE("Renderer: a fixed resolution is scaled by whole factors and letterboxed", "[renderer][targets]") {
    Scene scene({.resolution = {320, 180}, .letterboxColor = {.r = 0.25F, .g = 0.5F, .b = 0.75F, .a = 1.0F}},
                {1000, 600});
    CHECK(scene.renderer->sceneSize({1000, 600}) == UVec2{320, 180});
    CHECK(scene.renderer->targetSize({}, {1000, 600}) == UVec2{320, 180});

    const std::vector<std::string> composite = compositeLines(scene.render({}));
    REQUIRE(composite.size() == 7);
    CHECK(composite[0] ==
          std::format("BeginRenderPass color0={} load=Clear store=Store clear=(0.25, 0.5, 0.75, 1)",
                      scene.target.texture));
    // 3x (960x540) is the largest factor that fits 1000x600, centered on whole pixels.
    CHECK(composite[1] == "SetViewport x=20 y=30 width=960 height=540 minDepth=0 maxDepth=1");
    CHECK(composite[2] == "SetScissor x=20 y=30 width=960 height=540");
    CHECK(composite[3].starts_with("BindPipeline"));
    CHECK(composite[4].starts_with("BindGroup index=2"));
    CHECK(composite[5] == "Draw vertexCount=3 instanceCount=1 firstVertex=0 firstInstance=0");
    CHECK(composite[6] == "EndRenderPass");
}

TEST_CASE("Renderer: composite rectangles", "[renderer][targets]") {
    SECTION("whole factors, centered") {
        CHECK(compositeRect({320, 180}, {1280, 720}) ==
              CompositeRect{.position = {0.0F, 0.0F}, .size = {1280.0F, 720.0F}, .scale = 4.0F});
        CHECK(compositeRect({320, 180}, {1920, 1200}) ==
              CompositeRect{.position = {0.0F, 60.0F}, .size = {1920.0F, 1080.0F}, .scale = 6.0F});
        // The narrower side decides: 2x horizontally, 5x vertically.
        CHECK(compositeRect({320, 180}, {641, 1000}) ==
              CompositeRect{.position = {0.0F, 320.0F}, .size = {640.0F, 360.0F}, .scale = 2.0F});
        CHECK(compositeRect({64, 32}, {64, 32}) ==
              CompositeRect{.position = {0.0F, 0.0F}, .size = {64.0F, 32.0F}, .scale = 1.0F});
    }
    SECTION("an output smaller than the scene scales it down to fit") {
        const CompositeRect rect = compositeRect({320, 180}, {160, 120});
        CHECK(rect.scale == 0.5F);
        CHECK(rect.size == Vec2{160.0F, 90.0F});
        CHECK(rect.position == Vec2{0.0F, 15.0F});
    }
    SECTION("zero sizes give an empty rectangle") {
        CHECK(compositeRect({320, 180}, {0, 0}).size == Vec2{0.0F});
        CHECK(compositeRect({0, 0}, {640, 360}).size == Vec2{0.0F});
    }
    SECTION("output pixels map to scene pixels") {
        const CompositeRect rect = compositeRect({320, 180}, {1000, 600});
        CHECK(outputToScene(rect, {20.0F, 30.0F}) == Vec2{0.0F, 0.0F});
        CHECK(outputToScene(rect, {980.0F, 570.0F}) == Vec2{320.0F, 180.0F});
        CHECK(outputToScene(rect, {0.0F, 0.0F}).x < 0.0F); // in the letterbox
    }
    SECTION("the scene size follows the output unless a resolution is fixed") {
        CHECK(sceneTargetSize({}, {800, 600}) == UVec2{800, 600});
        CHECK(sceneTargetSize({.resolution = {320, 180}}, {800, 600}) == UVec2{320, 180});
    }
}

TEST_CASE("Renderer: resizing recreates the scene target without leaks", "[renderer][targets]") {
    Scene scene;
    const FrameTarget small = scene.target;
    const FrameTarget large = scene.output({128, 96});
    static_cast<void>(scene.render({}));
    static_cast<void>(scene.render({})); // both frame slots used
    const usize objects = scene.device->liveObjectCount();

    FrameTarget seen;
    scene.renderer->addPass(std::make_unique<TestPass>(
        "Reader", [](PassBuilder& builder) { builder.reads(PassResource::SceneColor); },
        [&](PassContext& context, rhi::CommandList& /*commands*/) {
            seen = context.target(PassResource::SceneColor);
        }));
    scene.target = large;
    static_cast<void>(scene.render({}));
    CHECK(seen.size == UVec2{128, 96});
    // The old color, depth and composite group wait for the frames that used them.
    CHECK(scene.device->pendingDestructionCount() >= 3);

    for (int frame = 0; frame < 6; ++frame) { // back and forth
        scene.target = frame % 2 == 0 ? small : large;
        static_cast<void>(scene.render({}));
    }
    scene.device->waitIdle();
    CHECK(scene.device->pendingDestructionCount() == 0);
    CHECK(scene.device->liveObjectCount() == objects);

    SECTION("changing the resolution recreates it too") {
        scene.renderer->setSceneTarget({.resolution = {16, 8}});
        static_cast<void>(scene.render({}));
        CHECK(seen.size == UVec2{16, 8});
        scene.device->waitIdle();
        CHECK(scene.device->liveObjectCount() == objects);
    }
}

TEST_CASE("Renderer: camera targets are created, sized and destroyed", "[renderer][targets]") {
    Scene scene;
    const usize objects = scene.device->liveObjectCount();

    CHECK_FALSE(scene.renderer->createRenderTarget({.size = {0, 16}}).has_value());
    const RenderTargetHandle minimap = *scene.renderer->createRenderTarget({.size = {48, 32}});
    const RenderTargetHandle flat =
        *scene.renderer->createRenderTarget({.size = {8, 8}, .depthFormat = rhi::Format::Undefined});
    REQUIRE(scene.renderer->renderTarget(minimap) != nullptr);
    CHECK(scene.renderer->renderTarget(minimap)->size == UVec2{48, 32});
    CHECK(scene.renderer->renderTarget(minimap)->depthFormat == rhi::Format::Depth32Float);
    CHECK(scene.renderer->renderTarget(flat)->depthTexture.isNull());
    CHECK(scene.renderer->targetSize(minimap, {640, 360}) == UVec2{48, 32});
    CHECK(scene.device->liveObjectCount() == objects + 3);

    scene.renderer->destroyRenderTarget(minimap);
    scene.renderer->destroyRenderTarget(flat);
    scene.renderer->destroyRenderTarget({}); // ignored
    CHECK(scene.renderer->renderTarget(minimap) == nullptr);
    CHECK(scene.renderer->targetSize(minimap, {640, 360}) == UVec2{0U});
    scene.device->waitIdle();
    CHECK(scene.device->liveObjectCount() == objects);
}

TEST_CASE("Renderer: steady-state frames do not allocate", "[renderer][allocation]") {
    Scene scene;
    scene.renderer->addPass(viewsPass("Views"));
    FramePacket packet;
    packet.views = {viewWithTime(1.0F, 1), viewWithTime(2.0F, 0)};
    const auto frame = [&] {
        scene.device->beginFrame();
        rhi::CommandList& commands = scene.device->acquireCommandList();
        scene.renderer->render(packet, scene.target, commands);
        scene.device->submit(commands);
        scene.device->endFrame();
    };
    for (int warmUp = 0; warmUp < 3; ++warmUp) { // both frame slots grow their lists
        frame();
    }

    u64 allocations = 0;
    {
        const test::AllocationCounter counter;
        frame();
        frame();
        allocations = counter.count();
    }
    CHECK(allocations == 0);
}

#if TESERRA_ENABLE_ASSERTS

TEST_CASE("Renderer: a pass using a resource it did not declare asserts", "[renderer][validation]") {
    Scene scene;
    scene.renderer->addPass(std::make_unique<TestPass>(
        "Sneaky", [](PassBuilder& /*builder*/) {},
        [](PassContext& context, rhi::CommandList& /*commands*/) {
            static_cast<void>(context.target(PassResource::SceneColor));
        }));

    CHECK(test::requireAssert([&] { static_cast<void>(scene.render({})); }).message() ==
          "Pass 'Sneaky' uses SceneColor without declaring it in declare()");
    scene.device->endFrame();
}

TEST_CASE("Renderer: a view of a destroyed target asserts and is left out", "[renderer][validation]") {
    Scene scene;
    scene.renderer->addPass(viewsPass("Views"));
    const RenderTargetHandle gone = *scene.renderer->createRenderTarget({.size = {8, 8}});
    scene.renderer->destroyRenderTarget(gone);
    FramePacket packet;
    packet.views = {RenderView{.target = gone}, RenderView{}};

    CHECK(test::requireAssert([&] { static_cast<void>(scene.render(packet)); }).message() ==
          std::format("A view renders into destroyed render target {}", gone));
    scene.device->endFrame();
}

#endif
