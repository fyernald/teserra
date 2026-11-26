// App with scenes: run(scene) activates it before the first frame, switches requested mid-frame take effect
// at the next frame boundary, only the active scene's Time advances, and the active scene's phases run in
// frame order.

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

TEST_CASE("App: the active scene's phases run in frame order", "[engine][app][scene][schedule]") {
    auto platform = std::make_unique<test::HeadlessPlatform>();
    platform->setFrameTimes({25ms});
    App app(headlessConfig(4), std::move(platform));
    scene::Scene& level = app.scenes().create("level");

    std::vector<std::string> log;
    const auto record = [&](ecs::Phase phase) {
        level.systems().add(phase, std::string(ecs::phaseName(phase)),
                            [&log, phase](ecs::SystemContext&) { log.emplace_back(ecs::phaseName(phase)); });
    };
    for (const ecs::Phase phase :
         {ecs::Phase::Startup, ecs::Phase::PreUpdate, ecs::Phase::FixedUpdate, ecs::Phase::Update,
          ecs::Phase::PostUpdate, ecs::Phase::Extract, ecs::Phase::Last}) {
        record(phase);
    }
    std::vector<u32> fixedStepsPerFrame;
    const int exitCode =
        app.run(level, {
                           .fixedUpdate = [&](App&, f32) { log.emplace_back("fixedUpdate"); },
                           .update = [&](App&, f32) { log.emplace_back("update"); },
                       });
    CHECK(exitCode == 0);

    // Split the log into frames at PreUpdate; Startup comes once, before the first.
    REQUIRE_FALSE(log.empty());
    CHECK(log.front() == "Startup");
    std::vector<std::vector<std::string>> frames;
    for (usize i = 1; i < log.size(); ++i) {
        if (log[i] == "PreUpdate") {
            frames.emplace_back();
        }
        REQUIRE_FALSE(frames.empty());
        frames.back().push_back(log[i]);
    }
    REQUIRE(frames.size() == 4);
    for (const std::vector<std::string>& frame : frames) {
        u32 steps = 0;
        for (const std::string& entry : frame) {
            steps += entry == "FixedUpdate" ? 1U : 0U;
        }
        fixedStepsPerFrame.push_back(steps);
        std::vector<std::string> expected{"PreUpdate"};
        for (u32 step = 0; step < steps; ++step) {
            expected.emplace_back("FixedUpdate");
            expected.emplace_back("fixedUpdate");
        }
        expected.insert(expected.end(), {"Update", "update", "PostUpdate", "Extract", "Last"});
        CHECK(frame == expected);
    }
    // 25 ms frames, 10 ms steps: 2, 3 (25 + 5 left over), 2, 3.
    CHECK(fixedStepsPerFrame == std::vector<u32>{2, 3, 2, 3});
}

TEST_CASE("App: a scene's Startup runs the first frame it is active", "[engine][app][scene][schedule]") {
    App app(headlessConfig(3), std::make_unique<test::HeadlessPlatform>());
    scene::Scene& menu = app.scenes().create("menu");
    scene::Scene& level = app.scenes().create("level");
    std::vector<std::string> log;
    menu.systems().add(ecs::Phase::Startup, "menu_startup",
                       [&](ecs::SystemContext&) { log.emplace_back("menu"); });
    level.systems().add(ecs::Phase::Startup, "level_startup",
                        [&](ecs::SystemContext&) { log.emplace_back("level"); });
    menu.systems().add(ecs::Phase::Update, "start_game", [&](ecs::SystemContext&) {
        log.emplace_back("start_game");
        app.scenes().switchTo(level);
    });

    CHECK(app.run(menu) == 0);
    CHECK(log == std::vector<std::string>{"menu", "start_game", "level"});
    CHECK(menu.started());
    CHECK(level.started());
    CHECK(level.time().frame() == 2);
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
