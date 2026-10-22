// App rendering without a GPU: the Null backend on the headless platform. The App records, submits and
// presents as with a real device, and the null device keeps the frame's command lists for inspection.

#include <teserra/engine/app.hpp>
#include <teserra/rhi_null/null_device.hpp>
#include <teserra/test/allocation_counter.hpp>

#include "headless_platform.hpp"
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <utility>

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

    // Frame 3 is minimized: nothing rendered or presented.
    CHECK(renderedSizes == std::vector<UVec2>{{640, 360}, {800, 600}, {320, 200}});
    CHECK(fixture.swapchain().presentCount() == 3);
    CHECK(fixture.device().frameCount() == 4);
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
