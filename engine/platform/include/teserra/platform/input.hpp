#pragma once

// Keyboard and mouse state built from engine events (docs/architecture/platform-abstraction.md#interfaces).
// Independent of the window backend: tests drive it with synthetic events.

#include <teserra/core/math.hpp>
#include <teserra/core/types.hpp>
#include <teserra/platform/event.hpp>
#include <teserra/platform/key.hpp>
#include <teserra/platform/mouse_button.hpp>

#include <bitset>
#include <string>
#include <string_view>

namespace teserra::platform {

/// Keyboard and mouse state for one frame. Each frame, call beginFrame(), then processEvent() for every
/// event Platform::pollEvents() returned; queries then describe that frame:
///
///     input.beginFrame();
///     events.clear();
///     platform.pollEvents(events);
///     for (const Event& event : events) {
///         input.processEvent(event);
///     }
///     if (input.wasPressed(Key::Space)) { jump(); }
///
/// Edges are recorded per event, so a key that goes down and up between two polls reports both
/// wasPressed() and wasReleased() in that frame. Losing focus releases every held key and button.
/// A plain value: copy it to keep a snapshot.
class Input {
public:
    /// Starts a frame: forgets the previous frame's presses, releases, mouse movement, scrolling and
    /// text. Held keys and buttons stay down.
    void beginFrame() noexcept;

    /// Applies one event. Window events other than FocusChanged are ignored.
    void processEvent(const Event& event);

    /// True while `key` is held.
    [[nodiscard]] bool isDown(Key key) const;
    /// True if `key` went down this frame (key repeat does not count).
    [[nodiscard]] bool wasPressed(Key key) const;
    /// True if `key` came up this frame, including the release caused by losing focus.
    [[nodiscard]] bool wasReleased(Key key) const;

    [[nodiscard]] bool isDown(MouseButton button) const;
    [[nodiscard]] bool wasPressed(MouseButton button) const;
    [[nodiscard]] bool wasReleased(MouseButton button) const;

    /// Last cursor position in window coordinates (see MouseMoved); (0, 0) until the first move.
    [[nodiscard]] Vec2 mousePosition() const noexcept { return m_mousePosition; }
    /// Cursor movement this frame in window coordinates. The first move after creation sets the
    /// position without a delta.
    [[nodiscard]] Vec2 mouseDelta() const noexcept { return m_mouseDelta; }
    /// Scrolling this frame in wheel notches (see MouseScrolled).
    [[nodiscard]] Vec2 scrollDelta() const noexcept { return m_scrollDelta; }
    /// Text typed this frame, UTF-8 encoded. Invalid code points are replaced by U+FFFD.
    [[nodiscard]] std::string_view text() const noexcept { return m_text; }

    /// Direction from four held keys, e.g. axis2D(Key::A, Key::D, Key::S, Key::W). Each axis is -1, 0 or
    /// +1 (opposite keys cancel), +Y is up (ADR-0024), and diagonals are normalized to length 1.
    [[nodiscard]] Vec2 axis2D(Key left, Key right, Key down, Key up) const;

private:
    void setKey(Key key, bool down);
    void setButton(MouseButton button, bool down);
    void moveMouse(Vec2 position) noexcept;
    void appendText(char32_t codepoint);
    void releaseAll() noexcept;

    std::bitset<kKeyCount> m_keysDown;
    std::bitset<kKeyCount> m_keysPressed;
    std::bitset<kKeyCount> m_keysReleased;
    std::bitset<kMouseButtonCount> m_buttonsDown;
    std::bitset<kMouseButtonCount> m_buttonsPressed;
    std::bitset<kMouseButtonCount> m_buttonsReleased;
    Vec2 m_mousePosition{0.0F};
    Vec2 m_mouseDelta{0.0F};
    Vec2 m_scrollDelta{0.0F};
    bool m_hasMousePosition = false;
    std::string m_text; // cleared, not freed, by beginFrame()
};

} // namespace teserra::platform
