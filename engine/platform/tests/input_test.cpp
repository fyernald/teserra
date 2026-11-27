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

// Gamepads (#133).

namespace {

GamepadConnectionChanged connect(u32 gamepad, bool connected = true) {
    return {.gamepad = gamepad, .connected = connected};
}

GamepadButtonChanged gamepadButton(u32 gamepad, GamepadButton button, bool down) {
    return {.gamepad = gamepad, .button = button, .down = down};
}

GamepadAxisMoved gamepadAxis(u32 gamepad, GamepadAxis axis, f32 value) {
    return {.gamepad = gamepad, .axis = axis, .value = value};
}

} // namespace

TEST_CASE("Input: gamepad buttons have edges like keys", "[platform][input][gamepad]") {
    Input input;
    CHECK_FALSE(input.isConnected(0));
    CHECK_FALSE(input.firstGamepad().has_value());

    frame(input, {connect(0), gamepadButton(0, GamepadButton::South, true)});
    CHECK(input.isConnected(0));
    CHECK(input.firstGamepad() == 0U);
    CHECK(input.wasPressed(0, GamepadButton::South));
    CHECK(input.isDown(0, GamepadButton::South));

    frame(input);
    CHECK_FALSE(input.wasPressed(0, GamepadButton::South));
    CHECK(input.isDown(0, GamepadButton::South));

    frame(input, {gamepadButton(0, GamepadButton::South, false)});
    CHECK(input.wasReleased(0, GamepadButton::South));
    CHECK_FALSE(input.isDown(0, GamepadButton::South));

    frame(input,
          {gamepadButton(0, GamepadButton::Start, true), gamepadButton(0, GamepadButton::Start, false)});
    CHECK(input.wasPressed(0, GamepadButton::Start));
    CHECK(input.wasReleased(0, GamepadButton::Start));
}

TEST_CASE("Input: two gamepads are independent", "[platform][input][gamepad]") {
    Input input;

    frame(input, {connect(0), connect(1), gamepadButton(1, GamepadButton::East, true),
                  gamepadAxis(0, GamepadAxis::LeftX, 1.0F), gamepadAxis(1, GamepadAxis::LeftX, -1.0F)});

    CHECK(input.isConnected(0));
    CHECK(input.isConnected(1));
    CHECK(input.isDown(1, GamepadButton::East));
    CHECK_FALSE(input.isDown(0, GamepadButton::East));
    CHECK(input.stick(0, GamepadStick::Left).x == Catch::Approx(1.0F));
    CHECK(input.stick(1, GamepadStick::Left).x == Catch::Approx(-1.0F));

    frame(input, {connect(0, false)});
    CHECK(input.firstGamepad() == 1U);
    CHECK(input.isDown(1, GamepadButton::East));
}

TEST_CASE("Input: disconnecting releases a gamepad's buttons and rests its axes",
          "[platform][input][gamepad]") {
    Input input;
    frame(input, {connect(2), gamepadButton(2, GamepadButton::DpadUp, true),
                  gamepadAxis(2, GamepadAxis::RightTrigger, 1.0F)});

    frame(input, {connect(2, false)});

    CHECK_FALSE(input.isConnected(2));
    CHECK(input.wasReleased(2, GamepadButton::DpadUp));
    CHECK_FALSE(input.isDown(2, GamepadButton::DpadUp));
    CHECK(input.axis(2, GamepadAxis::RightTrigger) == 0.0F);

    // Reconnected, the gamepad starts at rest and reports again what it holds.
    frame(input, {connect(2), gamepadButton(2, GamepadButton::DpadUp, true)});
    CHECK(input.wasPressed(2, GamepadButton::DpadUp));
}

TEST_CASE("Input: losing focus keeps gamepad buttons held", "[platform][input][gamepad]") {
    Input input;
    frame(input, {connect(0), gamepadButton(0, GamepadButton::West, true)});

    frame(input, {FocusChanged{false}});

    CHECK(input.isDown(0, GamepadButton::West));
    CHECK_FALSE(input.wasReleased(0, GamepadButton::West));
}

TEST_CASE("Input: a stick inside its deadzone is at rest", "[platform][input][gamepad]") {
    Input input;
    frame(input,
          {connect(0), gamepadAxis(0, GamepadAxis::LeftX, 0.1F), gamepadAxis(0, GamepadAxis::LeftY, -0.15F)});

    CHECK(input.stick(0, GamepadStick::Left) == Vec2{0.0F});
    CHECK(input.axis(0, GamepadAxis::LeftX) == 0.0F);
    CHECK(input.rawAxis(0, GamepadAxis::LeftX) == Catch::Approx(0.1F));
}

TEST_CASE("Input: outside the deadzone a stick is rescaled to reach 1", "[platform][input][gamepad]") {
    Input input;

    // Halfway between the deadzone (0.2) and the edge.
    frame(input, {connect(0), gamepadAxis(0, GamepadAxis::RightY, 0.6F)});
    CHECK(input.stick(0, GamepadStick::Right).y == Catch::Approx(0.5F));
    CHECK(input.axis(0, GamepadAxis::RightY) == Catch::Approx(0.5F));

    frame(input, {gamepadAxis(0, GamepadAxis::RightY, -1.0F)});
    CHECK(input.stick(0, GamepadStick::Right) == Vec2{0.0F, -1.0F});
}

TEST_CASE("Input: the stick deadzone is radial, so diagonals keep their direction",
          "[platform][input][gamepad]") {
    Input input;
    // Each axis alone is inside the deadzone, together they are not.
    frame(input,
          {connect(0), gamepadAxis(0, GamepadAxis::LeftX, 0.18F), gamepadAxis(0, GamepadAxis::LeftY, 0.18F)});
    const Vec2 direction = input.stick(0, GamepadStick::Left);
    CHECK(direction.x > 0.0F);
    CHECK(direction.x == Catch::Approx(direction.y));

    // A corner reading beyond the unit circle is clamped to length 1.
    frame(input, {gamepadAxis(0, GamepadAxis::LeftX, 1.0F), gamepadAxis(0, GamepadAxis::LeftY, 1.0F)});
    CHECK(math::length(input.stick(0, GamepadStick::Left)) == Catch::Approx(1.0F));
}

TEST_CASE("Input: triggers have their own deadzone", "[platform][input][gamepad]") {
    Input input;
    frame(input, {connect(0), gamepadAxis(0, GamepadAxis::LeftTrigger, 0.05F),
                  gamepadAxis(0, GamepadAxis::RightTrigger, 0.55F)});

    CHECK(input.axis(0, GamepadAxis::LeftTrigger) == 0.0F);
    CHECK(input.axis(0, GamepadAxis::RightTrigger) == Catch::Approx(0.5F));

    input.setGamepadDeadzones({.stick = 0.2F, .trigger = 0.0F});
    CHECK(input.axis(0, GamepadAxis::LeftTrigger) == Catch::Approx(0.05F));
    CHECK(input.gamepadDeadzones().trigger == 0.0F);
}

TEST_CASE("Input: gamepad names", "[platform][input][gamepad]") {
    CHECK(gamepadButtonName(GamepadButton::South) == "South");
    CHECK(gamepadButtonName(GamepadButton::DpadLeft) == "DpadLeft");
    CHECK(gamepadAxisName(GamepadAxis::RightTrigger) == "RightTrigger");
}

#if TESERRA_ENABLE_ASSERTS
TEST_CASE("Input: a gamepad index out of range asserts", "[platform][input][gamepad]") {
    Input input;

    const auto failure = test::requireAssert([&] { static_cast<void>(input.isConnected(kMaxGamepads)); });
    CHECK(failure.message() == std::string("Gamepad index 16 is not below kMaxGamepads (16)"));

    REQUIRE_ASSERTS(input.setGamepadDeadzones({.stick = 1.0F}));
}
#endif
