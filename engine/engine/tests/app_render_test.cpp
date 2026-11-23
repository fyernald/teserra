// App rendering without a GPU: the Null backend on the headless platform. The App records, submits and
// presents as with a real device, and the null device keeps the frame's command lists for inspection.

#include <teserra/engine/app.hpp>
#include <teserra/rhi/shader.hpp>
#include <teserra/rhi/shader_blob.hpp>
#include <teserra/rhi_null/null_device.hpp>
#include <teserra/test/allocation_counter.hpp>

#include "headless_platform.hpp"
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <format>
#include <memory>
#include <span>
#include <utility>
#include <vector>

using namespace teserra;
using namespace teserra::engine;

namespace {

AppConfig nullConfig(u64 frames) {
    return {.title = "Null render test",
            .windowSize = {640, 360},
            .backend = rhi::Backend::Null,
            .maxFrames = frames};
}

struct NullApp {
    explicit NullApp(AppConfig config)
        : NullApp(std::move(config), std::make_unique<test::HeadlessPlatform>()) {}

    NullApp(AppConfig config, std::unique_ptr<test::HeadlessPlatform> platform)
        : headless(*platform), app(std::move(config), std::move(platform)) {}

    rhi::null::NullDevice& device() { return static_cast<rhi::null::NullDevice&>(app.device()); }
    rhi::null::NullSwapchain& swapchain() {
        return static_cast<rhi::null::NullSwapchain&>(*app.device().swapchain());
    }

    test::HeadlessPlatform& headless;
    App app;
};

} // namespace

TEST_CASE("App on the Null backend: the rhi_clear frame is recorded headlessly", "[engine][app][render]") {
    NullApp fixture(nullConfig(3));
    REQUIRE(fixture.app.isInitialized());
    REQUIRE(fixture.app.device().backend() == rhi::Backend::Null);
    CHECK(fixture.headless.lastWindowDesc().graphicsApi == platform::GraphicsApi::None);

    // The render callback of examples/rhi_clear, with a fixed color.
    const int exitCode = fixture.app.run({.render = [](App& app, rhi::CommandList& commands) {
        commands.pushDebugGroup("Clear");
        commands.beginRenderPass({.colorAttachments = {{
                                      .texture = app.backbuffer(),
                                      .loadOp = rhi::LoadOp::Clear,
                                      .storeOp = rhi::StoreOp::Store,
                                      .clearColor = {0.25F, 0.5F, 0.75F, 1.0F},
                                  }}});
        commands.endRenderPass();
        commands.popDebugGroup();
    }});

    CHECK(exitCode == 0);
    CHECK(fixture.device().frameCount() == 3);
    CHECK(fixture.swapchain().presentCount() == 3);
    REQUIRE(fixture.device().submitted().size() == 1);
    CHECK(rhi::describeCommands(*fixture.device().submitted()[0]) ==
          "PushDebugGroup \"Clear\"\n"
          "  BeginRenderPass color0=0:1 load=Clear store=Store clear=(0.25, 0.5, 0.75, 1)\n"
          "  EndRenderPass\n"
          "PopDebugGroup\n");
}

TEST_CASE("App on the Null backend: without a render callback the frame is cleared",
          "[engine][app][render]") {
    NullApp fixture(nullConfig(1));

    CHECK(fixture.app.run() == 0);

    REQUIRE(fixture.device().submitted().size() == 1);
    CHECK(rhi::describeCommands(*fixture.device().submitted()[0]) ==
          "BeginRenderPass color0=0:1 load=Clear store=Store clear=(0, 0, 0, 1)\n"
          "EndRenderPass\n");
}

TEST_CASE("App on the Null backend: resizes reach the swapchain, minimized frames skip rendering",
          "[engine][app][render]") {
    auto platform = std::make_unique<test::HeadlessPlatform>();
    platform->queueEvent(2, platform::WindowResized{.windowSize = {800, 600}, .framebufferSize = {800, 600}});
    platform->queueEvent(3, platform::WindowResized{.windowSize = {0, 0}, .framebufferSize = {0, 0}});
    platform->queueEvent(4, platform::WindowResized{.windowSize = {320, 200}, .framebufferSize = {320, 200}});
    NullApp fixture(nullConfig(4), std::move(platform));
    std::vector<UVec2> renderedSizes;

    CHECK(fixture.app.run({.render = [&](App& app, rhi::CommandList& commands) {
        renderedSizes.push_back(app.device().swapchain()->size());
        commands.beginRenderPass({.colorAttachments = {{.texture = app.backbuffer()}}});
        commands.endRenderPass();
    }}) == 0);

    // Frame 3 is minimized: paused, nothing rendered or presented, no device frame. Frame 4 waited for its
    // events instead of polling, and resumed with them.
    CHECK(renderedSizes == std::vector<UVec2>{{640, 360}, {800, 600}, {320, 200}});
    CHECK(fixture.swapchain().presentCount() == 3);
    CHECK(fixture.device().frameCount() == 3);
    CHECK(fixture.headless.waits() == 1);
}

TEST_CASE("App on the Null backend: a minimized window pauses the loop, which waits for events",
          "[engine][app][render]") {
    using namespace std::chrono_literals;
    auto platform = std::make_unique<test::HeadlessPlatform>();
    platform->setFrameTimes({10ms});
    platform->queueEvent(2, platform::WindowResized{.windowSize = {0, 0}, .framebufferSize = {0, 0}});
    platform->queueEvent(6, platform::WindowResized{.windowSize = {640, 360}, .framebufferSize = {640, 360}});
    AppConfig config = nullConfig(8);
    config.fixedStep = 10ms;
    config.minimizedEventWait = 100ms;
    NullApp fixture(std::move(config), std::move(platform));

    std::vector<u64> updatedFrames;
    std::vector<f32> deltas;
    std::vector<bool> paused;
    u32 fixedSteps = 0;
    CHECK(fixture.app.run({
              .onEvent = [&](App& app, const platform::Event&) { paused.push_back(app.isPaused()); },
              .fixedUpdate = [&](App&, f32) { ++fixedSteps; },
              .update =
                  [&](App& app, f32 deltaSeconds) {
                      updatedFrames.push_back(app.frameCount());
                      deltas.push_back(deltaSeconds);
                  },
          }) == 0);

    // Frame 2 (index 1) gets the minimize and pauses; frames 3 to 6 wait, and frame 6 gets the restore.
    CHECK(fixture.headless.polls() == 8);
    CHECK(fixture.headless.waits() == 4);
    CHECK(updatedFrames == std::vector<u64>{0, 5, 6, 7});
    CHECK(fixedSteps == 4); // one 10 ms step per running frame, none for the 330 ms spent minimized
    // The frame that resumes does not catch up the pause: it sees one frame time.
    REQUIRE(deltas.size() == 4);
    CHECK(deltas[1] < 0.011F);
    CHECK(fixture.swapchain().presentCount() == 4);
    CHECK(paused == std::vector<bool>{false, true}); // as each event was handled
    CHECK_FALSE(fixture.app.isPaused());
}

TEST_CASE("App on the Null backend: a close request while minimized ends the run", "[engine][app][render]") {
    auto platform = std::make_unique<test::HeadlessPlatform>();
    platform->queueEvent(1, platform::WindowResized{.windowSize = {0, 0}, .framebufferSize = {0, 0}});
    platform->queueEvent(3, platform::WindowCloseRequested{});
    NullApp fixture(nullConfig(0), std::move(platform));
    CHECK(fixture.app.run() == 0);
    CHECK(fixture.app.frameCount() == 3);
    CHECK(fixture.headless.waits() == 2);
    CHECK(fixture.swapchain().presentCount() == 0);
}

TEST_CASE("App on the Null backend: steady-state frames do not allocate",
          "[engine][app][render][allocation]") {
    NullApp fixture(nullConfig(0));
    u64 allocations = 0;
    std::unique_ptr<test::AllocationCounter> counter;

    CHECK(fixture.app.run({.render = [&](App& app, rhi::CommandList& commands) {
        commands.beginRenderPass(
            {.colorAttachments = {{.texture = app.backbuffer(), .loadOp = rhi::LoadOp::Clear}}});
        commands.endRenderPass();
        // Frames 1 to 3 warm up (both frame slots); frames 4 to 10 are counted.
        if (app.frameCount() == 3) {
            counter = std::make_unique<test::AllocationCounter>();
        } else if (app.frameCount() == 10) {
            allocations = counter->count();
            app.quit();
        }
    }}) == 0);

    CHECK(allocations == 0);
}

TEST_CASE("App on the Null backend: the rhi_triangle frame is recorded and validated headlessly",
          "[engine][app][render]") {
    NullApp fixture(nullConfig(2));
    REQUIRE(fixture.app.isInitialized());
    rhi::Device& device = fixture.app.device();

    // The resources and render callback of examples/rhi_triangle, with placeholder shader code: the null
    // backend compiles nothing.
    constexpr std::array<f32, usize{3} * 7> kVertices{
        0.0F,  0.5F,  0.0F, 1.0F, 0.0F, 0.0F, 1.0F, //
        -0.5F, -0.5F, 0.0F, 0.0F, 1.0F, 0.0F, 1.0F, //
        0.5F,  -0.5F, 0.0F, 0.0F, 0.0F, 1.0F, 1.0F, //
    };
    constexpr std::array<u16, 3> kIndices{0, 1, 2};
    constexpr std::array kBuffers{rhi::VertexBufferLayout{.stride = 7 * sizeof(f32)}};
    constexpr std::array kAttributes{
        rhi::VertexAttribute{.location = 0, .format = rhi::VertexFormat::Float3},
        rhi::VertexAttribute{.location = 1, .format = rhi::VertexFormat::Float4, .offset = 3 * sizeof(f32)},
    };
    const rhi::BufferHandle vertices =
        *device.createBuffer({.sizeInBytes = sizeof(kVertices),
                              .usage = rhi::BufferUsage::Vertex | rhi::BufferUsage::CopyDst,
                              .debugName = "Triangle vertices"});
    const rhi::BufferHandle indices =
        *device.createBuffer({.sizeInBytes = sizeof(kIndices),
                              .usage = rhi::BufferUsage::Index | rhi::BufferUsage::CopyDst,
                              .debugName = "Triangle indices"});
    device.writeBuffer(vertices, 0, std::as_bytes(std::span(kVertices)));
    device.writeBuffer(indices, 0, std::as_bytes(std::span(kIndices)));
    // .tshader blobs; the null backend reads their stage and reflection.
    constexpr std::array<u32, 2> kSpirvWords{0x07230203, 0x00010500};
    const auto blob = [&](rhi::ShaderStage stage) {
        return rhi::writeShaderBlob({.stage = stage, .spirv = std::as_bytes(std::span(kSpirvWords))});
    };
    const std::vector<std::byte> vertexBlob = blob(rhi::ShaderStage::Vertex);
    const std::vector<std::byte> fragmentBlob = blob(rhi::ShaderStage::Fragment);
    const rhi::ShaderModuleHandle vertexShader =
        *device.createShaderModule({.stage = rhi::ShaderStage::Vertex, .blob = vertexBlob});
    const rhi::ShaderModuleHandle fragmentShader =
        *device.createShaderModule({.stage = rhi::ShaderStage::Fragment, .blob = fragmentBlob});
    const rhi::GraphicsPipelineHandle pipeline = *device.createGraphicsPipeline({
        .vertexShader = vertexShader,
        .fragmentShader = fragmentShader,
        .vertexBuffers = kBuffers,
        .vertexAttributes = kAttributes,
        .raster = {.cullMode = rhi::CullMode::Back},
        .targets = {.colorFormats = {device.swapchain()->format()}},
        .debugName = "Triangle",
    });

    rhi::TextureHandle image;
    CHECK(fixture.app.run({.render = [&](App& app, rhi::CommandList& commands) {
        image = app.backbuffer();
        commands.pushDebugGroup("Triangle");
        commands.beginRenderPass({.colorAttachments = {{.texture = app.backbuffer(),
                                                        .loadOp = rhi::LoadOp::Clear,
                                                        .clearColor = {0.05F, 0.05F, 0.06F, 1.0F}}}});
        commands.bindPipeline(pipeline);
        commands.bindVertexBuffer(0, vertices);
        commands.bindIndexBuffer(indices, rhi::IndexType::Uint16);
        commands.drawIndexed({.indexCount = 3});
        commands.endRenderPass();
        commands.popDebugGroup();
    }}) == 0);

    REQUIRE(fixture.device().submitted().size() == 1);
    CHECK(rhi::describeCommands(*fixture.device().submitted()[0]) ==
          std::format(
              "PushDebugGroup \"Triangle\"\n"
              "  BeginRenderPass color0={} load=Clear store=Store clear=(0.05, 0.05, 0.06, 1)\n"
              "    BindPipeline pipeline={}\n"
              "    BindVertexBuffers firstSlot=0 buffers=[{}+0]\n"
              "    BindIndexBuffer buffer={}+0 indexType=Uint16\n"
              "    DrawIndexed indexCount=3 instanceCount=1 firstIndex=0 vertexOffset=0 firstInstance=0\n"
              "  EndRenderPass\n"
              "PopDebugGroup\n",
              image, pipeline, vertices, indices));
    // The uploads reached the buffers.
    CHECK(std::ranges::equal(fixture.device().bufferData(indices), std::as_bytes(std::span(kIndices))));

    device.destroy(pipeline);
    device.destroy(vertexShader);
    device.destroy(fragmentShader);
    device.destroy(indices);
    device.destroy(vertices);
}
