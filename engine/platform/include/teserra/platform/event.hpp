#pragma once

// Engine events (docs/architecture/platform-abstraction.md#interfaces). Platform::pollEvents() translates
// what the window system reports into these values and appends them to an EventQueue; nothing is
// delivered through callbacks from inside the window system. Input turns the keyboard, mouse and text
// events into per-frame state.

#include <teserra/core/math.hpp>
#include <teserra/core/types.hpp>
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

using Event = std::variant<WindowResized, WindowCloseRequested, FocusChanged, KeyChanged, MouseButtonChanged,
                           MouseMoved, MouseScrolled, TextEntered>;

/// Events collected by Platform::pollEvents(), in the order they happened. Reused across frames: clear()
/// keeps the capacity, so steady-state polling does not allocate. Main thread only.
class EventQueue {
public:
    void push(const Event& event) { m_events.push_back(event); }
    void clear() noexcept { m_events.clear(); }

    [[nodiscard]] std::span<const Event> events() const noexcept { return m_events; }
    [[nodiscard]] usize size() const noexcept { return m_events.size(); }
    [[nodiscard]] bool empty() const noexcept { return m_events.empty(); }

    [[nodiscard]] auto begin() const noexcept { return m_events.begin(); }
    [[nodiscard]] auto end() const noexcept { return m_events.end(); }

private:
    std::vector<Event> m_events;
};

} // namespace teserra::platform
