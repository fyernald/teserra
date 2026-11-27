#pragma once

// Engine events (docs/architecture/platform-abstraction.md#interfaces). Platform::pollEvents() translates
// what the window system reports into these values and appends them to an EventQueue; nothing is
// delivered through callbacks from inside the window system, except the one that draws while a window is
// resized (Platform::setLiveResizeHandler()). Input turns the keyboard, mouse, text and gamepad events into
// per-frame state.

#include <teserra/core/math.hpp>
#include <teserra/core/types.hpp>
#include <teserra/platform/gamepad.hpp>
#include <teserra/platform/key.hpp>
#include <teserra/platform/mouse_button.hpp>

#include <span>
#include <variant>
#include <vector>

namespace teserra::platform {

/// The window changed size. Both sizes can be 0 while the window is minimized.
struct WindowResized {
    UVec2 windowSize;      ///< In screen coordinates.
    UVec2 framebufferSize; ///< In pixels; what rendering uses.

    friend bool operator==(const WindowResized&, const WindowResized&) = default;
};

/// The user asked to close the window (close button, Alt+F4). Window::shouldClose() is now true; the
/// application decides whether to quit.
struct WindowCloseRequested {
    friend bool operator==(const WindowCloseRequested&, const WindowCloseRequested&) = default;
};

/// The window gained or lost keyboard focus.
struct FocusChanged {
    bool focused;

    friend bool operator==(const FocusChanged&, const FocusChanged&) = default;
};

/// A key went down, came up, or repeats while held. When the window loses focus, keys still held are
/// reported as released.
struct KeyChanged {
    Key key;
    bool down;   ///< True if the key is now down.
    bool repeat; ///< True for the operating system's key repeat while held (then `down` is true).

    friend bool operator==(const KeyChanged&, const KeyChanged&) = default;
};

/// A mouse button went down or came up.
struct MouseButtonChanged {
    MouseButton button;
    bool down; ///< True if the button is now down.

    friend bool operator==(const MouseButtonChanged&, const MouseButtonChanged&) = default;
};

/// The cursor moved over the window.
struct MouseMoved {
    /// In window coordinates (screen coordinates, see Window::windowSize()): origin at the top-left of
    /// the window's content area, +Y down. Can be outside the window while a button is held.
    Vec2 position;

    friend bool operator==(const MouseMoved&, const MouseMoved&) = default;
};

/// The mouse wheel or touchpad scrolled.
struct MouseScrolled {
    /// In wheel notches; +Y is away from the user (scrolling up), +X is to the right.
    Vec2 offset;

    friend bool operator==(const MouseScrolled&, const MouseScrolled&) = default;
};

/// A character was typed, after the keyboard layout and dead keys were applied. Use this, not
/// KeyChanged, for text.
struct TextEntered {
    char32_t codepoint; ///< A Unicode code point.

    friend bool operator==(const TextEntered&, const TextEntered&) = default;
};

/// A gamepad was connected or disconnected. A connected gamepad then reports its buttons that are down and
/// its axes away from rest; a disconnected one reports nothing more (Input releases its buttons).
struct GamepadConnectionChanged {
    u32 gamepad; ///< Index, below kMaxGamepads.
    bool connected;

    friend bool operator==(const GamepadConnectionChanged&, const GamepadConnectionChanged&) = default;
};

/// A gamepad button went down or came up. Gamepads are polled, so a press and release between two polls is
/// not seen.
struct GamepadButtonChanged {
    u32 gamepad;
    GamepadButton button;
    bool down; ///< True if the button is now down.

    friend bool operator==(const GamepadButtonChanged&, const GamepadButtonChanged&) = default;
};

/// A gamepad axis changed. The value is raw, without a deadzone (see Input::stick()).
struct GamepadAxisMoved {
    u32 gamepad;
    GamepadAxis axis;
    f32 value; ///< -1 to 1 for stick axes (+Y up), 0 to 1 for triggers.

    friend bool operator==(const GamepadAxisMoved&, const GamepadAxisMoved&) = default;
};

/// Anything the platform reports during a frame.
using Event = std::variant<WindowResized, WindowCloseRequested, FocusChanged, KeyChanged, MouseButtonChanged,
                           MouseMoved, MouseScrolled, TextEntered, GamepadConnectionChanged,
                           GamepadButtonChanged, GamepadAxisMoved>;

/// Events collected by Platform::pollEvents(), in the order they happened. Reused across frames: clear()
/// keeps the capacity, so steady-state polling does not allocate. Main thread only.
class EventQueue {
public:
    /// Appends `event`.
    void push(const Event& event) { m_events.push_back(event); }
    /// Removes every event, keeping the memory.
    void clear() noexcept { m_events.clear(); }

    /// The events in the order they arrived.
    [[nodiscard]] std::span<const Event> events() const noexcept { return m_events; }
    [[nodiscard]] usize size() const noexcept { return m_events.size(); }
    /// Whether there are no events.
    [[nodiscard]] bool empty() const noexcept { return m_events.empty(); }

    /// Iteration over the events, oldest first.
    [[nodiscard]] auto begin() const noexcept { return m_events.begin(); }
    [[nodiscard]] auto end() const noexcept { return m_events.end(); }

private:
    std::vector<Event> m_events;
};

} // namespace teserra::platform
