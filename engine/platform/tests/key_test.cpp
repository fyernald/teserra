#include <teserra/platform/key.hpp>

#include <teserra/platform/mouse_button.hpp>

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string_view>

using namespace teserra;
using namespace teserra::platform;

TEST_CASE("Key: every key has a unique name", "[platform][input]") {
    std::set<std::string_view> names;
    for (usize index = 0; index < kKeyCount; ++index) {
        const std::string_view name = keyName(static_cast<Key>(index));
        CAPTURE(index, name);
        CHECK_FALSE(name.empty());
        CHECK(names.insert(name).second);
    }
    CHECK(keyName(Key::W) == "W");
    CHECK(keyName(Key::Digit7) == "7");
    CHECK(keyName(Key::LeftShift) == "Left Shift");
    CHECK(keyName(Key::RightSuper) == "Right Super"); // the last key
}

TEST_CASE("MouseButton: every button has a unique name", "[platform][input]") {
    std::set<std::string_view> names;
    for (usize index = 0; index < kMouseButtonCount; ++index) {
        const std::string_view name = mouseButtonName(static_cast<MouseButton>(index));
        CAPTURE(index, name);
        CHECK_FALSE(name.empty());
        CHECK(names.insert(name).second);
    }
    CHECK(mouseButtonName(MouseButton::Forward) == "Forward"); // the last button
}
