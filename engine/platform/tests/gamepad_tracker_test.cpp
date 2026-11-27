#include "common/gamepad_tracker.hpp"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace teserra;
using namespace teserra::platform;

namespace {

std::vector<Event> update(GamepadTracker& tracker, u32 gamepad, const GamepadSnapshot& snapshot) {
    EventQueue queue;
    tracker.update(gamepad, snapshot, queue);
    return {queue.begin(), queue.end()};
}

GamepadSnapshot connected() {
    return GamepadSnapshot{.connected = true};
}

} // namespace

TEST_CASE("GamepadTracker: nothing happens while a gamepad is absent", "[platform][gamepad]") {
    GamepadTracker tracker;

    CHECK(update(tracker, 0, GamepadSnapshot{}).empty());
    CHECK_FALSE(tracker.wasConnected(0));
}

TEST_CASE("GamepadTracker: a new gamepad reports its connection, then what it holds", "[platform][gamepad]") {
    GamepadTracker tracker;
    GamepadSnapshot snapshot = connected();
    snapshot.buttons[static_cast<usize>(GamepadButton::North)] = true;
    snapshot.axes[static_cast<usize>(GamepadAxis::LeftY)] = 0.5F;

    const std::vector<Event> events = update(tracker, 3, snapshot);

    CHECK(events == std::vector<Event>{
                        GamepadConnectionChanged{.gamepad = 3, .connected = true},
                        GamepadButtonChanged{.gamepad = 3, .button = GamepadButton::North, .down = true},
                        GamepadAxisMoved{.gamepad = 3, .axis = GamepadAxis::LeftY, .value = 0.5F},
                    });
    CHECK(tracker.wasConnected(3));
}

TEST_CASE("GamepadTracker: only changes are reported", "[platform][gamepad]") {
    GamepadTracker tracker;
    GamepadSnapshot snapshot = connected();
    update(tracker, 0, snapshot);
    CHECK(update(tracker, 0, snapshot).empty());

    snapshot.buttons[static_cast<usize>(GamepadButton::Start)] = true;
    snapshot.axes[static_cast<usize>(GamepadAxis::RightTrigger)] = 1.0F;
    CHECK(update(tracker, 0, snapshot) ==
          std::vector<Event>{
              GamepadButtonChanged{.gamepad = 0, .button = GamepadButton::Start, .down = true},
              GamepadAxisMoved{.gamepad = 0, .axis = GamepadAxis::RightTrigger, .value = 1.0F},
          });

    snapshot.buttons[static_cast<usize>(GamepadButton::Start)] = false;
    CHECK(update(tracker, 0, snapshot) == std::vector<Event>{GamepadButtonChanged{
                                              .gamepad = 0, .button = GamepadButton::Start, .down = false}});
}

TEST_CASE("GamepadTracker: a removed gamepad reports only its disconnection", "[platform][gamepad]") {
    GamepadTracker tracker;
    GamepadSnapshot snapshot = connected();
    snapshot.buttons[static_cast<usize>(GamepadButton::South)] = true;
    update(tracker, 1, snapshot);

    CHECK(update(tracker, 1, GamepadSnapshot{}) ==
          std::vector<Event>{GamepadConnectionChanged{.gamepad = 1, .connected = false}});
    CHECK_FALSE(tracker.wasConnected(1));

    // Plugged in again, it reports the button anew.
    CHECK(update(tracker, 1, snapshot).size() == 2);
}

TEST_CASE("GamepadTracker: gamepads are tracked separately", "[platform][gamepad]") {
    GamepadTracker tracker;
    update(tracker, 0, connected());

    CHECK(update(tracker, 1, connected()) ==
          std::vector<Event>{GamepadConnectionChanged{.gamepad = 1, .connected = true}});
    CHECK(update(tracker, 0, connected()).empty());
}
