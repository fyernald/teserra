// App with the real platform of the window backend. Skipped on machines without a window system, and the
// rendering test also on machines without an OpenGL 4.5 driver (the CI runners).

#include <teserra/engine/app.hpp>
#include <teserra/test/log_capture.hpp>

#include <catch2/catch_test_macros.hpp>

#include <format>
#include <string>

using namespace teserra;
using namespace teserra::engine;

TEST_CASE("App: runs frames on the real platform", "[engine][app][window]") {
    App app{{
        .title = "App window test",
        .windowSize = {320, 200},
        .windowVisible = false,
        .backend = std::nullopt,
        .maxFrames = 3,
    }};
    if (!app.isInitialized()) {
        REQUIRE(app.initError()->code == ErrorCode::DeviceFailure);
        SKIP("No window system: " << std::format("{}", *app.initError()));
    }
    int updates = 0;

    const int exitCode = app.run({.update = [&](App&, f32) { ++updates; }});

    CHECK(exitCode == 0);
    CHECK(updates == 3);
    CHECK(app.window().windowSize().x > 0);
    CHECK(app.frameTimer().elapsed().count() >= 0);
}

#if TESERRA_HAS_RHI_OPENGL
TEST_CASE("App: renders and presents through the OpenGL device", "[engine][app][window][gpu]") {
    const test::LogCapture log;
    App app{{
        .title = "App render test",
        .windowSize = {320, 200},
        .windowVisible = false,
        .backend = rhi::Backend::OpenGL,
        .maxFrames = 3,
    }};
    if (!app.isInitialized()) {
        REQUIRE(app.initError()->code == ErrorCode::DeviceFailure);
        SKIP("No window system or OpenGL 4.5 driver: " << std::format("{}", *app.initError()));
    }
    REQUIRE(app.device().backend() == rhi::Backend::OpenGL);
    int renders = 0;

    const int exitCode = app.run({.render = [&](App& running, rhi::CommandList& commands) {
        REQUIRE_FALSE(running.backbuffer().isNull());
        commands.beginRenderPass({.colorAttachments = {{
                                      .texture = running.backbuffer(),
                                      .loadOp = rhi::LoadOp::Clear,
                                      .clearColor = {0.2F, 0.4F, 0.6F, 1.0F},
                                  }}});
        commands.endRenderPass();
        ++renders;
    }});

    CHECK(exitCode == 0);
    CHECK(renders == 3);
    CHECK(app.backbuffer().isNull()); // only valid while rendering
    for (const test::CapturedLog& record : log.records()) {
        INFO(record.message);
        CHECK(record.level < LogLevel::Error);
    }
}
#endif
