#pragma once

// Engine events (docs/architecture/platform-abstraction.md#interfaces). Platform::pollEvents() translates
// what the window system reports into these values and appends them to an EventQueue; nothing is
// delivered through callbacks from inside the window system. Keyboard, mouse and text events are added
// with the input module.

#include <teserra/core/math.hpp>
#include <teserra/core/types.hpp>

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

using Event = std::variant<WindowResized, WindowCloseRequested, FocusChanged>;

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
