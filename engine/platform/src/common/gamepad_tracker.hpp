#pragma once

// Turns the state of each gamepad, read by a window backend at every poll, into engine events
// (docs/architecture/platform-abstraction.md#gamepads). Portable, so tests drive it with snapshots.

#include <teserra/core/types.hpp>
#include <teserra/platform/event.hpp>
#include <teserra/platform/gamepad.hpp>

#include <array>

namespace teserra::platform {

/// A gamepad's state at one poll, in the engine's conventions: stick axes -1 to 1 with +Y up, triggers 0
/// to 1.
struct GamepadSnapshot {
    bool connected = false;
    std::array<bool, kGamepadButtonCount> buttons{};
    std::array<f32, kGamepadAxisCount> axes{};
};

/// Remembers the last snapshot of every gamepad and reports what changed since.
class GamepadTracker {
public:
    /// Compares `current` with the last snapshot of `gamepad` and appends the differences to `queue`: on
    /// connection, GamepadConnectionChanged, then the buttons down and the axes away from rest; on
    /// disconnection, only GamepadConnectionChanged; otherwise each changed button and axis.
    void update(u32 gamepad, const GamepadSnapshot& current, EventQueue& queue);

    /// Whether the last snapshot of `gamepad` was connected.
    [[nodiscard]] bool wasConnected(u32 gamepad) const noexcept { return m_last[gamepad].connected; }

private:
    std::array<GamepadSnapshot, kMaxGamepads> m_last{};
};

} // namespace teserra::platform
