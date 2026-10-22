#include <teserra/engine/app.hpp>

#include <teserra/test/assert.hpp>
#include <teserra/test/log_capture.hpp>

#include "headless_platform.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <memory>
#include <utility>
#include <variant>
#include <vector>

using namespace teserra;
using namespace teserra::engine;
using namespace std::chrono_literals;

namespace {

AppConfig headlessConfig() {
    return {.title = "Headless test", .backend = std::nullopt};
}

// An App on a headless platform; `headless` stays valid while the App lives.
struct HeadlessApp {
    explicit HeadlessApp(AppConfig config = headlessConfig(), test::Journal* journal = nullptr)
        : HeadlessApp(std::move(config), std::make_unique<test::HeadlessPlatform>(journal)) {}

    HeadlessApp(AppConfig config, std::unique_ptr<test::HeadlessPlatform> platform)
        : headless(*platform), app(std::move(config), std::move(platform)) {}

    test::HeadlessPlatform& headless;
    App app;
};

// Fixed steps run in each frame, recorded by update().
struct StepCounter {
    std::vector<u32> stepsPerFrame;
    u32 steps = 0;

    AppCallbacks callbacks() {
        return {
            .fixedUpdate = [this](App&, f32) { ++steps; },
            .update =
                [this](App&, f32) {
                    stepsPerFrame.push_back(steps);
                    steps = 0;
                },
        };
    }
};

} // namespace

TEST_CASE("App: runs fixed and variable updates for scripted frame times", "[engine][app]") {
    AppConfig config = headlessConfig();
    config.fixedStep = 10ms;
    config.maxFrames = 4;
    HeadlessApp fixture{config};
    fixture.headless.setFrameTimes({25ms});
    std::vector<f32> deltas;
    std::vector<f32> stepLengths;

    const int exitCode = fixture.app.run({
        .fixedUpdate = [&](App&, f32 stepSeconds) { stepLengths.push_back(stepSeconds); },
        .update = [&](App&, f32 deltaSeconds) { deltas.push_back(deltaSeconds); },
    });

    CHECK(exitCode == 0);
    CHECK(fixture.app.frameCount() == 4);
    CHECK(fixture.headless.polls() == 4);
    REQUIRE(deltas.size() == 4);
    for (const f32 delta : deltas) {
        CHECK(delta == Catch::Approx(0.025F));
    }
    // 100 ms of frames is exactly 10 steps of 10 ms.
    REQUIRE(stepLengths.size() == 10);
    CHECK(std::ranges::all_of(stepLengths, [](f32 step) { return step == Catch::Approx(0.01F); }));
}

TEST_CASE("App: fixed steps carry over between frames", "[engine][app]") {
    AppConfig config = headlessConfig();
    config.fixedStep = 10ms;
    config.maxFrames = 4;
    HeadlessApp fixture{config};
    fixture.headless.setFrameTimes({25ms});
    StepCounter counter;

    static_cast<void>(fixture.app.run(counter.callbacks()));

    CHECK(counter.stepsPerFrame == std::vector<u32>{2, 3, 2, 3});
}

TEST_CASE("App: a stall runs at most maxFixedStepsPerFrame steps and warns", "[engine][app]") {
    AppConfig config = headlessConfig();
    config.fixedStep = 10ms;
    config.maxFixedStepsPerFrame = 5;
    config.maxFrames = 3;
    HeadlessApp fixture{config};
    fixture.headless.setFrameTimes({16ms, 1s, 16ms});
    StepCounter counter;
    const test::LogCapture logs;

    static_cast<void>(fixture.app.run(counter.callbacks()));

    // 16 ms -> 1 step (6 ms left); 1006 ms -> 100 due, 5 run; 22 ms -> 2.
    CHECK(counter.stepsPerFrame == std::vector<u32>{1, 5, 2});
    const auto warning = std::ranges::find_if(logs.records(), [](const test::CapturedLog& record) {
        return record.level == LogLevel::Warn && record.category == LogCategory::App;
    });
    REQUIRE(warning != logs.records().end());
    CHECK(warning->message == "Frame took 1000 ms: skipped 95 fixed steps to catch up");
}

TEST_CASE("App: --frames N stops after N frames, other arguments are left alone", "[engine][app]") {
    const std::array argv{"game", "--level", "2", "--frames", "3"};
    AppConfig config = headlessConfig();
    config.args = {static_cast<int>(argv.size()), argv.data()};
    HeadlessApp fixture{config};
    int updates = 0;

    const int exitCode = fixture.app.run({.update = [&](App&, f32) { ++updates; }});

    CHECK(exitCode == 0);
    CHECK(updates == 3);
    CHECK(fixture.app.frameCount() == 3);
    CHECK(fixture.app.config().maxFrames == 3);
    CHECK(fixture.headless.lastWindowDesc().visible);
}

TEST_CASE("App: --hidden-window creates an invisible window", "[engine][app]") {
    const std::array argv{"game", "--hidden-window"};
    AppConfig config = headlessConfig();
    config.title = "Hidden";
    config.windowSize = {640, 360};
    config.args = {static_cast<int>(argv.size()), argv.data()};

    const HeadlessApp fixture{config};

    REQUIRE(fixture.app.isInitialized());
    const platform::WindowDesc& desc = fixture.headless.lastWindowDesc();
    CHECK_FALSE(desc.visible);
    CHECK(desc.title == "Hidden");
    CHECK(desc.size == UVec2{640, 360});
    CHECK(desc.graphicsApi == platform::GraphicsApi::None);
}

TEST_CASE("App: a malformed --frames fails initialization and run() returns 1", "[engine][app]") {
    const auto badArguments = GENERATE(std::vector<const char*>{"game", "--frames", "abc"},
                                       std::vector<const char*>{"game", "--frames", "0"},
                                       std::vector<const char*>{"game", "--frames"});
    AppConfig config = headlessConfig();
    config.args = {static_cast<int>(badArguments.size()), badArguments.data()};
    const test::LogCapture logs;
    HeadlessApp fixture{config};
    bool updated = false;

    CHECK_FALSE(fixture.app.isInitialized());
    REQUIRE(fixture.app.initError() != nullptr);
    CHECK(fixture.app.initError()->code == ErrorCode::InvalidArgument);
    CHECK(fixture.app.run({.update = [&](App&, f32) { updated = true; }}) == 1);
    CHECK_FALSE(updated);
    CHECK(fixture.headless.polls() == 0);
    CHECK(std::ranges::any_of(logs.records(), [](const test::CapturedLog& record) {
        return record.level == LogLevel::Error &&
               record.message.starts_with("Cannot start 'Headless test': ");
    }));
}

#if TESERRA_ENABLE_ASSERTS
TEST_CASE("App: services assert when initialization failed", "[engine][app]") {
    const std::array argv{"game", "--frames", "x"};
    AppConfig config = headlessConfig();
    config.args = {static_cast<int>(argv.size()), argv.data()};
    HeadlessApp fixture{config};

    REQUIRE_ASSERTS(static_cast<void>(fixture.app.window()));
    REQUIRE_ASSERTS(static_cast<void>(fixture.app.platform()));
    REQUIRE_ASSERTS(static_cast<void>(fixture.app.device()));
}

TEST_CASE("App: without a backend there is no device", "[engine][app]") {
    HeadlessApp fixture;

    REQUIRE(fixture.app.isInitialized());
    REQUIRE_ASSERTS(static_cast<void>(fixture.app.device()));
    CHECK(fixture.app.backbuffer().isNull());
}
#endif

TEST_CASE("App: a backend the build does not include fails initialization", "[engine][app]") {
    AppConfig config = headlessConfig();
    config.backend = rhi::Backend::Vulkan;
    HeadlessApp fixture{config};

    REQUIRE_FALSE(fixture.app.isInitialized());
    CHECK(fixture.app.initError()->code == ErrorCode::Unsupported);
    CHECK(fixture.app.initError()->message == "The Vulkan backend is not part of this build");
    CHECK(fixture.headless.lastWindowDesc().graphicsApi == platform::GraphicsApi::None);
}

#if TESERRA_HAS_RHI_OPENGL
TEST_CASE("App: the OpenGL backend asks for an OpenGL window", "[engine][app]") {
    AppConfig config = headlessConfig();
    config.backend = rhi::Backend::OpenGL;
    HeadlessApp fixture{config};

    // The headless platform's windows have no context, so the device cannot be created on them.
    CHECK(fixture.headless.lastWindowDesc().graphicsApi == platform::GraphicsApi::OpenGL);
    REQUIRE_FALSE(fixture.app.isInitialized());
    CHECK(fixture.app.initError()->code == ErrorCode::Unsupported);
    CHECK(fixture.app.initError()->message ==
          "The OpenGL backend needs a window created with GraphicsApi::OpenGL");
}
#endif

TEST_CASE("App: a null platform fails initialization", "[engine][app]") {
    App app{headlessConfig(), nullptr};

    CHECK_FALSE(app.isInitialized());
    CHECK(app.initError()->code == ErrorCode::InvalidArgument);
    CHECK(app.run() == 1);
}

TEST_CASE("App: a close request ends the loop after that frame", "[engine][app]") {
    HeadlessApp fixture;
    fixture.headless.queueEvent(2, platform::WindowCloseRequested{});
    int updates = 0;

    const int exitCode = fixture.app.run({.update = [&](App&, f32) { ++updates; }});

    CHECK(exitCode == 0);
    CHECK(updates == 2); // the frame that saw the request still runs
    CHECK(fixture.app.frameCount() == 2);
}

TEST_CASE("App: quit(exitCode) ends the loop and sets the exit code", "[engine][app]") {
    HeadlessApp fixture;

    const int exitCode = fixture.app.run({.update = [](App& app, f32) {
        if (app.frameCount() == 2) { // third frame
            app.quit(3);
        }
    }});

    CHECK(exitCode == 3);
    CHECK(fixture.app.frameCount() == 3);
}

TEST_CASE("App: a close request does not override the game's exit code", "[engine][app]") {
    HeadlessApp fixture;
    fixture.headless.queueEvent(
        1, platform::KeyChanged{.key = platform::Key::Escape, .down = true, .repeat = false});
    fixture.headless.queueEvent(1, platform::WindowCloseRequested{});

    const int exitCode = fixture.app.run({.onEvent = [](App& app, const platform::Event& event) {
        const auto* key = std::get_if<platform::KeyChanged>(&event);
        if (key != nullptr && key->key == platform::Key::Escape) {
            app.quit(7);
        }
    }});

    CHECK(exitCode == 7);
}

TEST_CASE("App: onEvent sees events after Input processed them", "[engine][app]") {
    AppConfig config = headlessConfig();
    config.maxFrames = 2;
    HeadlessApp fixture{config};
    fixture.headless.queueEvent(1,
                                platform::KeyChanged{.key = platform::Key::W, .down = true, .repeat = false});
    bool downInOnEvent = false;
    std::vector<bool> pressedInUpdate;

    static_cast<void>(fixture.app.run({
        .onEvent = [&](App& app,
                       const platform::Event&) { downInOnEvent = app.input().isDown(platform::Key::W); },
        .update = [&](App& app, f32) { pressedInUpdate.push_back(app.input().wasPressed(platform::Key::W)); },
    }));

    CHECK(downInOnEvent);
    CHECK(pressedInUpdate == std::vector<bool>{true, false}); // edges last one frame
}

TEST_CASE("App: services are destroyed in reverse order of creation", "[engine][app]") {
    test::Journal journal;
    {
        const HeadlessApp fixture{headlessConfig(), &journal};
        REQUIRE(fixture.app.isInitialized());
        CHECK(journal.empty());
    }

    CHECK(journal == test::Journal{"window destroyed", "platform destroyed"});
}

TEST_CASE("App: several Apps can exist side by side", "[engine][app]") {
    AppConfig first = headlessConfig();
    first.maxFrames = 2;
    AppConfig second = headlessConfig();
    second.maxFrames = 5;
    HeadlessApp a{first};
    HeadlessApp b{second};

    CHECK(a.app.run() == 0);
    CHECK(b.app.run() == 0);

    CHECK(a.app.frameCount() == 2);
    CHECK(b.app.frameCount() == 5);
    CHECK(&a.app.window() != &b.app.window());
}
