// App with scenes: run(scene) activates it before the first frame, switches requested mid-frame take effect
// at the next frame boundary, and only the active scene's Time advances.

#include <teserra/engine/app.hpp>
#include <teserra/test/assert.hpp>

#include "headless_platform.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

using namespace teserra;
using namespace teserra::engine;
using namespace std::chrono_literals;

namespace {

AppConfig headlessConfig(u64 maxFrames) {
    return {
        .title = "Headless scene test", .backend = std::nullopt, .fixedStep = 10ms, .maxFrames = maxFrames};
}

} // namespace

TEST_CASE("App: run(scene) activates the scene and switches only at frame boundaries",
          "[engine][app][scene]") {
    auto platform = std::make_unique<test::HeadlessPlatform>();
    test::HeadlessPlatform& headless = *platform;
    headless.setFrameTimes({20ms});
    App app(headlessConfig(4), std::move(platform));
    scene::Scene& menu = app.scenes().create("menu");
    scene::Scene& level = app.scenes().create("level");

    std::vector<std::string> fixedScenes;  // the active scene in each fixed step
    std::vector<std::string> updateScenes; // and in each update
    const auto activeName = [&] { return std::string(app.scenes().active()->name()); };
    const int exitCode =
        app.run(menu, {
                          .fixedUpdate =
                              [&](App&, f32) {
                                  fixedScenes.push_back(activeName());
                                  if (app.frameCount() == 1) {
                                      app.scenes().switchTo(level); // in frame 1's fixed steps
                                  }
                              },
                          .update =
                              [&](App&, f32) {
                                  updateScenes.push_back(activeName());
                                  if (app.frameCount() == 1) {
                                      app.scenes().switchTo(level); // and its update
                                  }
                              },
                      });

    CHECK(exitCode == 0);
    // 20 ms frames, 10 ms steps: two steps per frame. Frame 1 (the second) keeps the menu throughout.
    CHECK(fixedScenes ==
          std::vector<std::string>{"menu", "menu", "menu", "menu", "level", "level", "level", "level"});
    CHECK(updateScenes == std::vector<std::string>{"menu", "menu", "level", "level"});

    // Each scene's Time advanced only while it was active: two frames of 20 ms each.
    CHECK(menu.time().frame() == 2);
    CHECK(menu.time().elapsedSeconds() == Catch::Approx(0.04));
    CHECK(level.time().frame() == 2);
    CHECK(level.time().deltaSeconds() == Catch::Approx(0.02F));
    CHECK(level.time().fixedStepSeconds() == Catch::Approx(0.01F));
}

TEST_CASE("App: run(callbacks) leaves scenes alone", "[engine][app][scene]") {
    App app(headlessConfig(2), std::make_unique<test::HeadlessPlatform>());
    scene::Scene& level = app.scenes().create("level");
    CHECK(app.run() == 0);
    CHECK(app.scenes().active() == nullptr);
    CHECK(level.time().frame() == 0);
}

#if TESERRA_ENABLE_ASSERTS

TEST_CASE("App: run() with a scene of another App asserts", "[engine][app][scene][validation]") {
    App app(headlessConfig(1), std::make_unique<test::HeadlessPlatform>());
    scene::SceneManager other;
    scene::Scene& foreign = other.create("foreign");
    CHECK(test::requireAssert([&] { static_cast<void>(app.run(foreign)); }).message() ==
          "App::run() needs a scene created with App::scenes()");
}

#endif
