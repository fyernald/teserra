// The App's debug UI (#298), headless: the toggle key opens and closes it and never reaches the game, nor
// does the text it types; a closed debug UI runs no ImGui frame; the Stats window and a game's windows are
// listed and drawn while open; a click on a debug window and text typed into a debug text field do not reach
// the game, while releases and input elsewhere do; on the null backend, the debug UI is drawn over the frame
// after the game's render callback. Built only with TESERRA_WITH_DEBUG_UI.

#include <teserra/core/log.hpp>
#include <teserra/debug_ui/log_console.hpp>
#include <teserra/engine/app.hpp>
#include <teserra/rhi/command_list.hpp>
#include <teserra/rhi_null/null_device.hpp>
#include <teserra/scene/scene.hpp>
#include <teserra/scene/transform.hpp>

#include "headless_platform.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <imgui.h>

#include <array>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

using namespace teserra;
using namespace teserra::engine;

namespace {

AppConfig headlessConfig(u64 frames) {
    return {.title = "Debug UI test", .backend = std::nullopt, .maxFrames = frames};
}

struct HeadlessApp {
    explicit HeadlessApp(AppConfig config)
        : HeadlessApp(std::move(config), std::make_unique<test::HeadlessPlatform>()) {}
    HeadlessApp(AppConfig config, std::unique_ptr<test::HeadlessPlatform> platform)
        : headless(*platform), app(std::move(config), std::move(platform)) {}

    test::HeadlessPlatform& headless;
    App app;
};

// What the game saw: every event onEvent got, and per frame whether the debug UI was open and in a frame.
struct GameView {
    std::vector<platform::Event> events;
    std::vector<bool> open;
    std::vector<bool> inFrame;

    AppCallbacks callbacks() {
        return {
            .onEvent = [this](App&, const platform::Event& event) { events.push_back(event); },
            .update =
                [this](App& app, f32) {
                    open.push_back(app.isDebugUiOpen());
                    inFrame.push_back(app.debugUi().inFrame());
                },
        };
    }

    template <typename T>
    [[nodiscard]] std::vector<T> seen() const {
        std::vector<T> found;
        for (const platform::Event& event : events) {
            if (const auto* typed = std::get_if<T>(&event)) {
                found.push_back(*typed);
            }
        }
        return found;
    }
};

platform::KeyChanged key(platform::Key key, bool down) {
    return {.key = key, .down = down, .repeat = false};
}

void pressKey(test::HeadlessPlatform& headless, u64 poll, platform::Key pressed, char32_t text) {
    headless.queueEvent(poll, key(pressed, true));
    headless.queueEvent(poll, platform::TextEntered{.codepoint = text});
    headless.queueEvent(poll, key(pressed, false));
}

int imguiFrameCount(App& app) {
    ImGui::SetCurrentContext(app.debugUi().context());
    return ImGui::GetFrameCount();
}

} // namespace

TEST_CASE("App debug UI: the backtick opens and closes it, and the game sees neither the key nor its text",
          "[engine][app][debug_ui]") {
    HeadlessApp fixture(headlessConfig(5));
    pressKey(fixture.headless, 2, platform::Key::GraveAccent, U'`');
    pressKey(fixture.headless, 4, platform::Key::GraveAccent, U'`');
    pressKey(fixture.headless, 4, platform::Key::A, U'a');
    GameView game;

    REQUIRE(fixture.app.run(game.callbacks()) == 0);

    CHECK(game.open == std::vector<bool>{false, true, true, false, false});
    CHECK(game.inFrame == game.open); // ImGui:: calls work in update() exactly while it is open
    CHECK(game.seen<platform::KeyChanged>() ==
          std::vector<platform::KeyChanged>{key(platform::Key::A, true), key(platform::Key::A, false)});
    CHECK(game.seen<platform::TextEntered>() == std::vector<platform::TextEntered>{{.codepoint = U'a'}});
    CHECK_FALSE(fixture.app.input().isDown(platform::Key::GraveAccent));
}

TEST_CASE("App debug UI: --debug-ui and startOpen open it from the first frame; another key can toggle it",
          "[engine][app][debug_ui]") {
    SECTION("--debug-ui") {
        const std::array<const char*, 2> argv{"game", "--debug-ui"};
        AppConfig config = headlessConfig(2);
        config.args = {.argc = 2, .argv = argv.data()};
        HeadlessApp fixture(std::move(config));
        GameView game;
        REQUIRE(fixture.app.run(game.callbacks()) == 0);
        CHECK(game.open == std::vector<bool>{true, true});
    }
    SECTION("startOpen and a custom toggle key") {
        AppConfig config = headlessConfig(3);
        config.debugUi = {.toggleKey = platform::Key::F12, .startOpen = true};
        HeadlessApp fixture(std::move(config));
        fixture.headless.queueEvent(2, key(platform::Key::GraveAccent, true)); // an ordinary key now
        fixture.headless.queueEvent(3, key(platform::Key::F12, true));
        GameView game;
        REQUIRE(fixture.app.run(game.callbacks()) == 0);
        CHECK(game.open == std::vector<bool>{true, true, false});
        CHECK(game.seen<platform::KeyChanged>() ==
              std::vector<platform::KeyChanged>{key(platform::Key::GraveAccent, true)});
    }
}

TEST_CASE("App debug UI: disabled, there is none and the backtick goes to the game",
          "[engine][app][debug_ui]") {
    AppConfig config = headlessConfig(2);
    config.debugUi.enabled = false;
    HeadlessApp fixture(std::move(config));
    fixture.headless.queueEvent(1, key(platform::Key::GraveAccent, true));
    std::vector<platform::Event> events;
    REQUIRE(fixture.app.run(
                {.onEvent = [&](App&, const platform::Event& event) { events.push_back(event); }}) == 0);
    CHECK_FALSE(fixture.app.isDebugUiOpen());
    REQUIRE(events.size() == 1);
    CHECK(std::get<platform::KeyChanged>(events[0]) == key(platform::Key::GraveAccent, true));
}

TEST_CASE("App debug UI: a closed debug UI runs no ImGui frame", "[engine][app][debug_ui]") {
    HeadlessApp fixture(headlessConfig(8));
    fixture.app.setDebugUiOpen(true);
    fixture.headless.queueEvent(4, key(platform::Key::GraveAccent, true)); // closes it from frame 4
    std::vector<int> frameCounts;
    REQUIRE(fixture.app.run(
                {.update = [&](App& app, f32) { frameCounts.push_back(imguiFrameCount(app)); }}) == 0);
    CHECK(frameCounts == std::vector<int>{1, 2, 3, 3, 3, 3, 3, 3});
}

TEST_CASE("App debug UI: Stats and the game's windows are listed, and drawn while open",
          "[engine][app][debug_ui]") {
    HeadlessApp fixture(headlessConfig(4));
    int drawn = 0;
    fixture.app.debugUi().addWindow("Game", [&] {
        ImGui::TextUnformatted("coins");
        ++drawn;
    });
    std::vector<std::string> names;
    for (const debugui::DebugWindow& window : fixture.app.debugUi().windows()) {
        names.push_back(window.name);
    }
    CHECK(names ==
          std::vector<std::string>{"Stats", "Log", "Entities", "Inspector", "Assets", "Renderer", "Game"});
    CHECK_FALSE(fixture.app.debugUi().windows()[1].open); // the log opens from the View menu

    fixture.headless.queueEvent(3, key(platform::Key::GraveAccent, true));
    REQUIRE(fixture.app.run() == 0);
    CHECK(drawn == 2); // frames 3 and 4
}

TEST_CASE("App debug UI: a click on one of its windows does not reach the game, one elsewhere does",
          "[engine][app][debug_ui]") {
    AppConfig config = headlessConfig(11);
    config.debugUi.startOpen = true;
    HeadlessApp fixture(std::move(config));
    fixture.app.debugUi().addWindow("Panel", [] {
        ImGui::SetWindowPos({100.0F, 100.0F});
        ImGui::SetWindowSize({200.0F, 150.0F});
    });
    const platform::MouseButtonChanged down{.button = platform::MouseButton::Left, .down = true};
    const platform::MouseButtonChanged up{.button = platform::MouseButton::Left, .down = false};
    // Over the panel from frame 1. A window shows from its second frame and ImGui sees what the mouse is over
    // a frame later, so by frame 5 the click is the panel's.
    fixture.headless.queueEvent(1, platform::MouseMoved{.position = {150.0F, 150.0F}});
    fixture.headless.queueEvent(5, down);
    fixture.headless.queueEvent(5, up);
    fixture.headless.queueEvent(5, platform::MouseScrolled{.offset = {0.0F, 1.0F}});
    // Far from every window; the click of frame 10 is the game's.
    fixture.headless.queueEvent(7, platform::MouseMoved{.position = {1200.0F, 650.0F}});
    fixture.headless.queueEvent(10, down);
    fixture.headless.queueEvent(10, up);
    GameView game;

    REQUIRE(fixture.app.run(game.callbacks()) == 0);

    CHECK(game.seen<platform::MouseButtonChanged>() ==
          std::vector<platform::MouseButtonChanged>{up, down, up});
    CHECK(game.seen<platform::MouseScrolled>().empty());
    CHECK(game.seen<platform::MouseMoved>().size() == 2); // the cursor's position always reaches the game
}

TEST_CASE("App debug UI: text typed into one of its text fields does not reach the game",
          "[engine][app][debug_ui]") {
    AppConfig config = headlessConfig(6);
    config.debugUi.startOpen = true;
    HeadlessApp fixture(std::move(config));
    std::array<char, 32> text{};
    fixture.app.debugUi().addWindow("Console", [&] {
        ImGui::SetKeyboardFocusHere();
        ImGui::InputText("##input", text.data(), text.size());
    });
    pressKey(fixture.headless, 4, platform::Key::H, U'h');
    GameView game;

    REQUIRE(fixture.app.run(game.callbacks()) == 0);

    CHECK(std::string(text.data()) == "h");
    CHECK(game.seen<platform::TextEntered>().empty());
    // The release still reaches the game, so a key it saw pressed before the field took the keyboard is
    // released.
    CHECK(game.seen<platform::KeyChanged>() ==
          std::vector<platform::KeyChanged>{key(platform::Key::H, false)});
}

TEST_CASE("App debug UI on the null backend: drawn over the frame after the render callback, only while open",
          "[engine][app][debug_ui][render]") {
    AppConfig config{.title = "Debug UI render test",
                     .windowSize = {640, 360},
                     .backend = rhi::Backend::Null,
                     .maxFrames = 3};
    config.debugUi.startOpen = GENERATE(true, false);
    HeadlessApp fixture(config);
    REQUIRE(fixture.app.isInitialized());

    REQUIRE(fixture.app.run({.render = [](App& app, rhi::CommandList& commands) {
        commands.pushDebugGroup("Game");
        commands.beginRenderPass(
            {.colorAttachments = {{.texture = app.backbuffer(), .loadOp = rhi::LoadOp::Clear}}});
        commands.endRenderPass();
        commands.popDebugGroup();
    }}) == 0);

    auto& device = static_cast<rhi::null::NullDevice&>(fixture.app.device());
    REQUIRE(device.submitted().size() == 1);
    const std::string described = rhi::describeCommands(*device.submitted()[0]);
    const usize game = described.find(R"(PushDebugGroup "Game")");
    const usize debugUi = described.find(R"(PushDebugGroup "Debug UI")");
    REQUIRE(game != std::string::npos);
    if (config.debugUi.startOpen) {
        REQUIRE(debugUi != std::string::npos);
        CHECK(debugUi > game);
        CHECK(described.find("DrawIndexed", debugUi) != std::string::npos);
    } else {
        CHECK(debugUi == std::string::npos);
    }
}

TEST_CASE("App debug UI: the log console has the App's messages, also from before it was opened",
          "[engine][app][debug_ui]") {
    HeadlessApp fixture(headlessConfig(2));
    TESERRA_LOG_WARN(LogCategory::Game, "logged while the debug UI is closed");
    fixture.headless.queueEvent(2, key(platform::Key::GraveAccent, true));
    REQUIRE(fixture.app.debugUi().setWindowOpen("Log", true));
    REQUIRE(fixture.app.run() == 0);

    bool found = false;
    bool running = false;
    for (const debugui::LogEntry& entry : fixture.app.logConsole().entries()) {
        found = found || entry.message == "logged while the debug UI is closed";
        running = running || entry.message == "Running 'Debug UI test'";
    }
    CHECK(found);
    CHECK(running);
}

TEST_CASE("App debug UI: the inspector shows a selected entity of the running scene until it is destroyed",
          "[engine][app][debug_ui]") {
    AppConfig config = headlessConfig(4);
    config.debugUi.startOpen = true;
    HeadlessApp fixture(std::move(config));
    scene::Scene& level = fixture.app.scenes().create("level");
    const ecs::Entity hero = level.world().create();
    level.world().add<scene::Transform>(hero);
    REQUIRE(fixture.app.debugUi().setWindowOpen("Entities", true));
    REQUIRE(fixture.app.debugUi().setWindowOpen("Inspector", true));
    fixture.app.entityInspector().select(level, hero);

    std::vector<bool> selected;
    REQUIRE(fixture.app.run(level, {.update = [&](App& app, f32) {
                                selected.push_back(app.entityInspector().selectedEntity() == hero);
                                if (app.frameCount() == 1) {
                                    level.world().destroy(hero); // before the windows draw
                                }
                            }}) == 0);
    CHECK(selected == std::vector<bool>{true, true, false, false});
}

TEST_CASE("App debug UI on the null backend: the Assets and Renderer windows draw",
          "[engine][app][debug_ui]") {
    AppConfig config{.title = "Debug UI windows test", .backend = rhi::Backend::Null, .maxFrames = 3};
    config.debugUi.startOpen = true;
    HeadlessApp fixture(std::move(config));
    REQUIRE(fixture.app.isInitialized());
    for (const char* name : {"Assets", "Renderer"}) {
        REQUIRE(fixture.app.debugUi().setWindowOpen(name, true));
    }
    REQUIRE(fixture.app.run() == 0);
    CHECK(fixture.app.debugUi().drawData()->TotalVtxCount > 0);
}
