// DebugUi windows: games add windows that the View menu lists; open windows draw their contents each frame,
// closed ones do not.

#include <teserra/debug_ui/debug_ui.hpp>
#include <teserra/test/assert.hpp>

#include <catch2/catch_test_macros.hpp>
#include <imgui_internal.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace teserra;
using namespace teserra::debugui;

namespace {

void frame(DebugUi& debugUi) {
    debugUi.beginFrame({400.0F, 300.0F}, Vec2{1.0F}, 1.0F / 60.0F);
    debugUi.drawWindows();
    debugUi.endFrame();
}

std::vector<std::string> windowNames(const DebugUi& debugUi) {
    std::vector<std::string> names;
    for (const DebugWindow& window : debugUi.windows()) {
        names.push_back(window.name);
    }
    return names;
}

} // namespace

TEST_CASE("DebugUi: added windows are listed in order and draw while open", "[debug_ui][windows]") {
    const std::unique_ptr<DebugUi> debugUi = *DebugUi::create();
    int statsDrawn = 0;
    int gameDrawn = 0;
    debugUi->addWindow("Stats", [&] {
        ImGui::TextUnformatted("stats");
        ++statsDrawn;
    });
    debugUi->addWindow("Game", [&] { ++gameDrawn; }, /*open=*/false);

    CHECK(windowNames(*debugUi) == std::vector<std::string>{"Stats", "Game"});
    frame(*debugUi);
    CHECK(statsDrawn == 1);
    CHECK(gameDrawn == 0);

    CHECK(debugUi->setWindowOpen("Game", true));
    CHECK(debugUi->setWindowOpen("Stats", false));
    CHECK_FALSE(debugUi->setWindowOpen("Missing", true));
    frame(*debugUi);
    CHECK(statsDrawn == 1);
    CHECK(gameDrawn == 1);
    CHECK(debugUi->windows()[0].open == false);
    CHECK(debugUi->windows()[1].open == true);
}

TEST_CASE("DebugUi: drawWindows() shows the main menu bar with the View menu", "[debug_ui][windows]") {
    const std::unique_ptr<DebugUi> debugUi = *DebugUi::create();
    debugUi->addWindow("Stats", [] {});
    frame(*debugUi);

    ImGui::SetCurrentContext(debugUi->context());
    const ImGuiWindow* menuBar = ImGui::FindWindowByName("##MainMenuBar");
    REQUIRE(menuBar != nullptr);
    CHECK(menuBar->Active);
    const ImGuiWindow* stats = ImGui::FindWindowByName("Stats");
    REQUIRE(stats != nullptr);
    CHECK(stats->Active);
    CHECK(debugUi->drawData()->CmdListsCount > 0);
}

namespace {

const ImGuiWindow& findWindow(const DebugUi& debugUi, const char* name) {
    ImGui::SetCurrentContext(debugUi.context());
    const ImGuiWindow* window = ImGui::FindWindowByName(name);
    REQUIRE(window != nullptr);
    return *window;
}

bool overlaps(const ImGuiWindow& a, const ImGuiWindow& b) {
    return a.Pos.x < b.Pos.x + b.Size.x && b.Pos.x < a.Pos.x + a.Size.x && a.Pos.y < b.Pos.y + b.Size.y &&
           b.Pos.y < a.Pos.y + a.Size.y;
}

std::function<void()> lines(int count) {
    return [count] {
        for (int line = 0; line < count; ++line) {
            ImGui::Text("Line %d of a debug window", line);
        }
    };
}

} // namespace

TEST_CASE("DebugUi: windows first show below the menu bar without covering each other",
          "[debug_ui][windows]") {
    // #308: every window opened at ImGui's default position, on top of each other.
    const std::unique_ptr<DebugUi> debugUi = *DebugUi::create();
    debugUi->addWindow("Stats", lines(5));
    debugUi->addWindow("Log", lines(3), /*open=*/false);
    debugUi->addWindow("Game", lines(2));
    // ImGui sizes a new window on its first frame and shows it from the second, where it is placed.
    for (int i = 0; i < 3; ++i) {
        frame(*debugUi);
    }

    const ImGuiWindow& menuBar = findWindow(*debugUi, "##MainMenuBar");
    const ImGuiWindow& stats = findWindow(*debugUi, "Stats");
    const ImGuiWindow& game = findWindow(*debugUi, "Game");
    CHECK(stats.Pos.y >= menuBar.Pos.y + menuBar.Size.y);
    CHECK(game.Pos.y >= menuBar.Pos.y + menuBar.Size.y);
    CHECK_FALSE(overlaps(stats, game));

    // Log opens later in a free spot too.
    debugUi->setWindowOpen("Log", true);
    for (int i = 0; i < 3; ++i) {
        frame(*debugUi);
    }
    const ImGuiWindow& log = findWindow(*debugUi, "Log");
    CHECK_FALSE(overlaps(log, stats));
    CHECK_FALSE(overlaps(log, game));
    CHECK(log.Pos.y >= menuBar.Pos.y + menuBar.Size.y);
}

TEST_CASE("DebugUi: a window the user moved stays there", "[debug_ui][windows]") {
    const std::unique_ptr<DebugUi> debugUi = *DebugUi::create();
    debugUi->addWindow("Stats", [] {});
    frame(*debugUi);
    frame(*debugUi);

    ImGui::SetCurrentContext(debugUi->context());
    ImGui::SetWindowPos("Stats", {150.0F, 120.0F});
    frame(*debugUi);
    frame(*debugUi);
    const ImGuiWindow& stats = findWindow(*debugUi, "Stats");
    CHECK(stats.Pos.x == 150.0F);
    CHECK(stats.Pos.y == 120.0F);

    // Closed and opened again, it is still there.
    debugUi->setWindowOpen("Stats", false);
    frame(*debugUi);
    debugUi->setWindowOpen("Stats", true);
    frame(*debugUi);
    frame(*debugUi);
    CHECK(findWindow(*debugUi, "Stats").Pos.x == 150.0F);
}

#if TESERRA_ENABLE_ASSERTS
TEST_CASE("DebugUi: two windows with the same name are refused", "[debug_ui][windows]") {
    const std::unique_ptr<DebugUi> debugUi = *DebugUi::create();
    debugUi->addWindow("Stats", [] {});
    REQUIRE_ASSERTS(debugUi->addWindow("Stats", [] {}));
}

TEST_CASE("DebugUi: drawWindows() outside a frame asserts", "[debug_ui][windows]") {
    const std::unique_ptr<DebugUi> debugUi = *DebugUi::create();
    REQUIRE_ASSERTS(debugUi->drawWindows());
}
#endif
