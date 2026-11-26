// DebugUi input: engine keys map to ImGui keys, and ImGui reports when it uses the mouse or the keyboard, so
// the App (#298) can keep that input from the game.

#include <teserra/debug_ui/debug_ui.hpp>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <set>
#include <string>

using namespace teserra;
using namespace teserra::debugui;

namespace {

constexpr Vec2 kDisplay{400.0F, 300.0F};

// Runs one frame with a 120x80 window at (20, 20), and, if `field` is not null, a text field focused in it.
void frame(DebugUi& debugUi, std::string* field = nullptr) {
    debugUi.beginFrame(kDisplay, Vec2{1.0F}, 1.0F / 60.0F);
    ImGui::SetNextWindowPos({20.0F, 20.0F});
    ImGui::SetNextWindowSize({120.0F, 80.0F});
    ImGui::Begin("Input test");
    if (field != nullptr) {
        static char buffer[32] = {}; // NOLINT(modernize-avoid-c-arrays): ImGui's text buffer
        ImGui::SetKeyboardFocusHere();
        ImGui::InputText("##field", buffer, sizeof(buffer));
        *field = buffer;
    }
    ImGui::End();
    debugUi.endFrame();
}

} // namespace

TEST_CASE("imguiKey: every engine key ImGui has maps to a distinct ImGui key", "[debug_ui][input]") {
    std::set<int> seen;
    for (auto value = static_cast<int>(platform::Key::A);
         value <= static_cast<int>(platform::Key::RightSuper); ++value) {
        const auto key = static_cast<platform::Key>(value);
        if (key == platform::Key::World1) {
            continue; // ImGui has no key for it
        }
        INFO("key " << platform::keyName(key));
        const ImGuiKey imgui = imguiKey(key);
        CHECK(imgui != ImGuiKey_None);
        CHECK(seen.insert(static_cast<int>(imgui)).second);
    }
    CHECK(imguiKey(platform::Key::Unknown) == ImGuiKey_None);
    CHECK(imguiKey(platform::Key::A) == ImGuiKey_A);
    CHECK(imguiKey(platform::Key::Digit7) == ImGuiKey_7);
    CHECK(imguiKey(platform::Key::F12) == ImGuiKey_F12);
    CHECK(imguiKey(platform::Key::Numpad3) == ImGuiKey_Keypad3);
}

TEST_CASE("DebugUi: uses the mouse over its windows and leaves it elsewhere", "[debug_ui][input]") {
    const std::unique_ptr<DebugUi> debugUi = *DebugUi::create();
    frame(*debugUi);

    debugUi->processEvent(platform::MouseMoved{.position = {60.0F, 50.0F}}); // inside the window
    frame(*debugUi);
    CHECK(debugUi->wantsMouse());

    debugUi->processEvent(platform::MouseMoved{.position = {300.0F, 250.0F}}); // outside
    frame(*debugUi);
    CHECK_FALSE(debugUi->wantsMouse());
}

TEST_CASE("DebugUi: typed text reaches a focused text field, and the keyboard is taken",
          "[debug_ui][input]") {
    const std::unique_ptr<DebugUi> debugUi = *DebugUi::create();
    std::string field;
    frame(*debugUi, &field);
    frame(*debugUi, &field);

    for (const char32_t codepoint : {U'h', U'i'}) {
        debugUi->processEvent(platform::TextEntered{.codepoint = codepoint});
    }
    frame(*debugUi, &field);
    frame(*debugUi, &field);

    CHECK(field == "hi");
    CHECK(debugUi->wantsKeyboard());
}

TEST_CASE("DebugUi: focus loss releases held modifiers", "[debug_ui][input]") {
    const std::unique_ptr<DebugUi> debugUi = *DebugUi::create();
    debugUi->processEvent(
        platform::KeyChanged{.key = platform::Key::LeftControl, .down = true, .repeat = false});
    frame(*debugUi);
    CHECK(ImGui::GetIO().KeyCtrl);

    debugUi->processEvent(platform::FocusChanged{.focused = false});
    debugUi->processEvent(
        platform::KeyChanged{.key = platform::Key::LeftControl, .down = false, .repeat = false});
    frame(*debugUi);
    CHECK_FALSE(ImGui::GetIO().KeyCtrl);
}
