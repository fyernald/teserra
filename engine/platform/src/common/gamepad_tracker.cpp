#include "common/gamepad_tracker.hpp"

#include <teserra/core/assert.hpp>

namespace teserra::platform {

void GamepadTracker::update(u32 gamepad, const GamepadSnapshot& current, EventQueue& queue) {
    TESERRA_ASSERT(gamepad < kMaxGamepads, "Gamepad index {} is not below kMaxGamepads ({})", gamepad,
                   kMaxGamepads);
    GamepadSnapshot& last = m_last[gamepad];
    if (current.connected != last.connected) {
        queue.push(GamepadConnectionChanged{.gamepad = gamepad, .connected = current.connected});
        // A new gamepad is compared with one at rest; a removed one reports nothing more.
        last = GamepadSnapshot{.connected = current.connected};
        if (!current.connected) {
            return;
        }
    } else if (!current.connected) {
        return;
    }

    for (usize index = 0; index < kGamepadButtonCount; ++index) {
        if (current.buttons[index] != last.buttons[index]) {
            queue.push(GamepadButtonChanged{.gamepad = gamepad,
                                            .button = static_cast<GamepadButton>(index),
                                            .down = current.buttons[index]});
        }
    }
    for (usize index = 0; index < kGamepadAxisCount; ++index) {
        if (current.axes[index] != last.axes[index]) {
            queue.push(GamepadAxisMoved{
                .gamepad = gamepad, .axis = static_cast<GamepadAxis>(index), .value = current.axes[index]});
        }
    }
    last = current;
}

} // namespace teserra::platform
