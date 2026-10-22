#include <teserra/platform/input.hpp>

#include <teserra/test/assert.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <initializer_list>
#include <string>

using namespace teserra;
using namespace teserra::platform;

namespace {

// One frame of the main loop with synthetic events.
void frame(Input& input, std::initializer_list<Event> events = {}) {
    input.beginFrame();
    for (const Event& event : events) {
        input.processEvent(event);
    }
}

KeyChanged keyDown(Key key) {
    return {.key = key, .down = true, .repeat = false};
}

KeyChanged keyUp(Key key) {
    return {.key = key, .down = false, .repeat = false};
}

KeyChanged keyRepeat(Key key) {
    return {.key = key, .down = true, .repeat = true};
}

} // namespace

TEST_CASE("Input: wasPressed is true for exactly one frame after a key-down event", "[platform][input]") {
    Input input;

    frame(input, {keyDown(Key::Space)});
    CHECK(input.wasPressed(Key::Space));
    CHECK(input.isDown(Key::Space));
    CHECK_FALSE(input.wasReleased(Key::Space));

    frame(input);
    CHECK_FALSE(input.wasPressed(Key::Space));
    CHECK(input.isDown(Key::Space));

    frame(input, {keyUp(Key::Space)});
    CHECK(input.wasReleased(Key::Space));
    CHECK_FALSE(input.isDown(Key::Space));

    frame(input);
    CHECK_FALSE(input.wasReleased(Key::Space));
    CHECK_FALSE(input.isDown(Key::Space));
}

TEST_CASE("Input: keys are independent", "[platform][input]") {
    Input input;

    frame(input, {keyDown(Key::W), keyDown(Key::LeftShift)});
    frame(input, {keyUp(Key::W)});

    CHECK(input.wasReleased(Key::W));
    CHECK(input.isDown(Key::LeftShift));
    CHECK_FALSE(input.wasReleased(Key::LeftShift));
    CHECK_FALSE(input.isDown(Key::A));
}

TEST_CASE("Input: a tap within one frame reports both edges", "[platform][input]") {
    Input input;

    frame(input, {keyDown(Key::Enter), keyUp(Key::Enter)});

    CHECK(input.wasPressed(Key::Enter));
    CHECK(input.wasReleased(Key::Enter));
    CHECK_FALSE(input.isDown(Key::Enter));
}

TEST_CASE("Input: key repeat and duplicate events do not create edges", "[platform][input]") {
    Input input;
    frame(input, {keyDown(Key::Left)});

    frame(input, {keyRepeat(Key::Left), keyRepeat(Key::Left), keyDown(Key::Left)});
    CHECK(input.isDown(Key::Left));
    CHECK_FALSE(input.wasPressed(Key::Left));

    frame(input, {keyUp(Key::Left)});
    frame(input, {keyUp(Key::Left)});
    CHECK_FALSE(input.wasReleased(Key::Left));
}

TEST_CASE("Input: Key::Unknown is ignored", "[platform][input]") {
    Input input;

    frame(input, {keyDown(Key::Unknown)});

    CHECK_FALSE(input.isDown(Key::Unknown));
    CHECK_FALSE(input.wasPressed(Key::Unknown));
}

TEST_CASE("Input: mouse buttons have per-frame edges", "[platform][input]") {
    Input input;

    frame(input, {MouseButtonChanged{MouseButton::Left, true}});
    CHECK(input.wasPressed(MouseButton::Left));
    CHECK(input.isDown(MouseButton::Left));
    CHECK_FALSE(input.isDown(MouseButton::Right));

    frame(input, {MouseButtonChanged{MouseButton::Left, true}});
    CHECK_FALSE(input.wasPressed(MouseButton::Left));

    frame(input, {MouseButtonChanged{MouseButton::Left, false}});
    CHECK(input.wasReleased(MouseButton::Left));
    CHECK_FALSE(input.isDown(MouseButton::Left));

    frame(input,
          {MouseButtonChanged{MouseButton::Forward, true}, MouseButtonChanged{MouseButton::Forward, false}});
    CHECK(input.wasPressed(MouseButton::Forward));
    CHECK(input.wasReleased(MouseButton::Forward));
}

TEST_CASE("Input: focus loss releases all held keys and buttons", "[platform][input]") {
    Input input;
    frame(input, {keyDown(Key::W), keyDown(Key::LeftControl), MouseButtonChanged{MouseButton::Right, true}});

    frame(input, {FocusChanged{false}});
    CHECK_FALSE(input.isDown(Key::W));
    CHECK_FALSE(input.isDown(Key::LeftControl));
    CHECK_FALSE(input.isDown(MouseButton::Right));
    CHECK(input.wasReleased(Key::W));
    CHECK(input.wasReleased(Key::LeftControl));
    CHECK(input.wasReleased(MouseButton::Right));
    CHECK_FALSE(input.wasReleased(Key::A)); // was not held

    // The window system's own release events after focus loss are duplicates.
    frame(input, {keyUp(Key::W)});
    CHECK_FALSE(input.wasReleased(Key::W));
}

TEST_CASE("Input: gaining focus changes nothing", "[platform][input]") {
    Input input;
    frame(input, {keyDown(Key::W)});

    frame(input, {FocusChanged{true}});

    CHECK(input.isDown(Key::W));
    CHECK_FALSE(input.wasReleased(Key::W));
}

TEST_CASE("Input: mouse position and per-frame delta", "[platform][input]") {
    Input input;
    CHECK(input.mousePosition() == Vec2{0.0F, 0.0F});

    // The first position is not a movement from (0, 0).
    frame(input, {MouseMoved{{100.0F, 50.0F}}});
    CHECK(input.mousePosition() == Vec2{100.0F, 50.0F});
    CHECK(input.mouseDelta() == Vec2{0.0F, 0.0F});

    frame(input, {MouseMoved{{110.0F, 45.0F}}, MouseMoved{{115.0F, 60.0F}}});
    CHECK(input.mousePosition() == Vec2{115.0F, 60.0F});
    CHECK(input.mouseDelta() == Vec2{15.0F, 10.0F});

    frame(input);
    CHECK(input.mousePosition() == Vec2{115.0F, 60.0F});
    CHECK(input.mouseDelta() == Vec2{0.0F, 0.0F});
}

TEST_CASE("Input: scrolling accumulates within a frame", "[platform][input]") {
    Input input;

    frame(input, {MouseScrolled{{0.0F, 1.0F}}, MouseScrolled{{0.5F, 2.0F}}});
    CHECK(input.scrollDelta() == Vec2{0.5F, 3.0F});

    frame(input);
    CHECK(input.scrollDelta() == Vec2{0.0F, 0.0F});
}

TEST_CASE("Input: text is UTF-8 and lasts one frame", "[platform][input]") {
    Input input;

    // 1-, 2-, 3- and 4-byte sequences: A, e acute, euro sign, and an emoji.
    frame(input, {TextEntered{U'A'}, TextEntered{U'é'}, TextEntered{U'€'}, TextEntered{U'\U0001F600'}});
    CHECK(input.text() == "A\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80");

    frame(input);
    CHECK(input.text().empty());
}

TEST_CASE("Input: invalid code points become U+FFFD", "[platform][input]") {
    Input input;

    frame(input, {TextEntered{0xD800}, TextEntered{0x110000}});

    CHECK(input.text() == "\xEF\xBF\xBD\xEF\xBF\xBD");
}

TEST_CASE("Input: axis2D", "[platform][input]") {
    Input input;
    const auto wasd = [&] { return input.axis2D(Key::A, Key::D, Key::S, Key::W); };

    CHECK(wasd() == Vec2{0.0F, 0.0F});

    frame(input, {keyDown(Key::W)});
    CHECK(wasd() == Vec2{0.0F, 1.0F}); // +Y is up

    frame(input, {keyDown(Key::A)});
    const Vec2 upLeft = wasd();
    CHECK(upLeft.x == Catch::Approx(-0.70710678F));
    CHECK(upLeft.y == Catch::Approx(0.70710678F));

    frame(input, {keyDown(Key::D)});
    CHECK(wasd() == Vec2{0.0F, 1.0F}); // A and D cancel

    frame(input, {keyUp(Key::W), keyDown(Key::S)});
    CHECK(wasd() == Vec2{0.0F, -1.0F});
}

TEST_CASE("Input: window events other than focus loss are ignored", "[platform][input]") {
    Input input;
    frame(input, {keyDown(Key::Q)});

    frame(input, {WindowResized{{640, 480}, {640, 480}}, WindowCloseRequested{}, FocusChanged{true}});

    CHECK(input.isDown(Key::Q));
    CHECK_FALSE(input.wasReleased(Key::Q));
    CHECK(input.text().empty());
}

TEST_CASE("Input: copies are snapshots", "[platform][input]") {
    Input input;
    frame(input, {keyDown(Key::E)});
    const Input snapshot = input;

    frame(input, {keyUp(Key::E)});

    CHECK(snapshot.isDown(Key::E));
    CHECK(snapshot.wasPressed(Key::E));
    CHECK_FALSE(input.isDown(Key::E));
}

#if TESERRA_ENABLE_ASSERTS
TEST_CASE("Input: an invalid Key value asserts", "[platform][input]") {
    const Input input;
    const auto invalid = static_cast<Key>(kKeyCount);

    const auto failure = test::requireAssert([&] { static_cast<void>(input.isDown(invalid)); });

    CHECK(failure.message() == std::string("Invalid Key value ") + std::to_string(kKeyCount));
}
#endif
