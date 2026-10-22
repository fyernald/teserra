// App with the real platform of the window backend. Skipped on machines without a window system.

#include <teserra/engine/app.hpp>

#include <catch2/catch_test_macros.hpp>

#include <format>

using namespace teserra;
using namespace teserra::engine;

TEST_CASE("App: runs frames on the real platform", "[engine][app][window]") {
    App app{{
        .title = "App window test",
        .windowSize = {320, 200},
        .windowVisible = false,
        .graphicsApi = platform::GraphicsApi::None,
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
