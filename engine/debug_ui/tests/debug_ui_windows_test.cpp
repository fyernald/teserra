// DebugUi windows: games add windows that the View menu lists; open windows draw their contents each frame,
// closed ones do not.

#include <teserra/debug_ui/debug_ui.hpp>
#include <teserra/test/assert.hpp>

#include <catch2/catch_test_macros.hpp>
#include <imgui_internal.h>

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
