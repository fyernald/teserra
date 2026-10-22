// App with the real platform of the window backend. Skipped on machines without a window system.

#include <teserra/engine/app.hpp>

#include <catch2/catch_test_macros.hpp>

#include <format>

using namespace teserra;
using namespace teserra::engine;

TEST_CASE("App: runs frames on the real platform", "[engine][app][window]") {
    AppConfig config;
    config.title = "App window test";
    config.windowSize = {320, 200};
    config.windowVisible = false;
    config.graphicsApi = platform::GraphicsApi::None;
    config.maxFrames = 3;
    App app{config};
    if (!app.isInitialized()) {
        REQUIRE(app.initError()->code == ErrorCode::DeviceFailure);
        SKIP("No window system: " << std::format("{}", *app.initError()));
    }
    int updates = 0;
    AppCallbacks callbacks;
    callbacks.update = [&](App&, f32) { ++updates; };

    const int exitCode = app.run(callbacks);

    CHECK(exitCode == 0);
    CHECK(updates == 3);
    CHECK(app.window().windowSize().x > 0);
    CHECK(app.frameTimer().elapsed().count() >= 0);
}
