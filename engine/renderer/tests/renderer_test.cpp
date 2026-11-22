// The renderer's frame structure on the null backend: passes run in the order they were added, each inside
// a debug group; they reach only the resources they declared; every view's camera is uploaded at an
// aligned offset of the frame's ring partition and bound at set 0 with that offset; and a steady-state frame
// allocates nothing.

#include <teserra/renderer/renderer.hpp>

#include <teserra/rhi_null/null_device.hpp>
#include <teserra/test/allocation_counter.hpp>
#include <teserra/test/assert.hpp>

#include <catch2/catch_test_macros.hpp>

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

// A renderer on a headless null device with a render target for SceneColor.
struct Scene {
    Scene() {
        renderer = *Renderer::create(*device, {.uploadBytesPerFrame = 4096});
        target = {.texture = *device->createTexture({.format = rhi::Format::Rgba8Srgb,
                                                     .width = 64,
                                                     .height = 32,
                                                     .usage = rhi::TextureUsage::RenderTarget,
                                                     .debugName = "Scene color"}),
                  .format = rhi::Format::Rgba8Srgb,
                  .size = {64, 32}};
    }
    ~Scene() {
        renderer.reset();
        device->destroy(target.texture);
    }
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&) = delete;
    Scene& operator=(Scene&&) = delete;

    // Renders one frame and returns its commands as describeCommands() lines.
    std::vector<std::string> render(const FramePacket& packet) {
        device->beginFrame();
        rhi::CommandList& commands = device->acquireCommandList();
        renderer->render(packet, target, commands);
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
};

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

TEST_CASE("Renderer: passes run in the order they were added, each in a debug group", "[renderer]") {
    Scene scene;
    std::vector<std::string> ran;
    for (const std::string_view name : {"Scene2D", "Debug", "Composite"}) {
        scene.renderer->addPass(std::make_unique<TestPass>(
            name, [](PassBuilder& /*builder*/) {},
            [&ran, name](PassContext& /*context*/, rhi::CommandList& /*commands*/) {
                ran.emplace_back(name);
            }));
    }

    const std::vector<std::string> lines = scene.render({});

    CHECK(ran == std::vector<std::string>{"Scene2D", "Debug", "Composite"});
    CHECK(lines == std::vector<std::string>{
                       "PushDebugGroup \"Scene2D\"",
                       "PopDebugGroup",
                       "PushDebugGroup \"Debug\"",
                       "PopDebugGroup",
                       "PushDebugGroup \"Composite\"",
                       "PopDebugGroup",
                   });
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

    const std::vector<std::string> binds = linesStartingWith(scene.render(packet), {"BindGroup"});

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
    const std::vector<std::string> next = linesStartingWith(scene.render(packet), {"BindGroup"});
    REQUIRE(next.size() == 3);
    CHECK(next[0].ends_with("dynamicOffsets=[4096]"));
}

TEST_CASE("Renderer: passes get the targets they declared", "[renderer]") {
    Scene scene;
    rhi::TextureHandle seen;
    scene.renderer->addPass(std::make_unique<TestPass>(
        "Reader", [](PassBuilder& builder) { builder.reads(PassResource::SceneColor); },
        [&](PassContext& context, rhi::CommandList& /*commands*/) {
            seen = context.target(PassResource::SceneColor).texture;
            CHECK(context.target(PassResource::SceneColor).size == UVec2{64, 32});
        }));

    static_cast<void>(scene.render({}));
    CHECK(seen == scene.target.texture);
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

#endif
