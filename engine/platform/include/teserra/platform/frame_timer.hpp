#pragma once

#include <teserra/core/types.hpp>

#include <chrono>

namespace teserra::platform {

/// Measures frames: the time between ticks, the time since the first tick, and a smoothed frame rate.
/// Fed explicit times, so it works with any Clock:
///
///     timer.tick(platform.clock().now()); // once per frame
///     update(timer.deltaSeconds());
///
/// A plain value. It reports real time; bounding simulation work after a stall is FixedStepAccumulator's
/// job.
class FrameTimer {
public:
    /// `fpsWindow` is how long fps() averages over before it updates; it must be positive (asserted).
    explicit FrameTimer(std::chrono::nanoseconds fpsWindow = std::chrono::milliseconds{500});

    /// Starts a frame at time `now`, which must not be earlier than the previous tick (asserted).
    void tick(std::chrono::nanoseconds now);

    /// Time between the last two ticks; zero after the first tick.
    [[nodiscard]] std::chrono::nanoseconds delta() const noexcept { return m_delta; }
    [[nodiscard]] f32 deltaSeconds() const noexcept;

    /// Time from the first tick to the last one.
    [[nodiscard]] std::chrono::nanoseconds elapsed() const noexcept { return m_last - m_first; }

    /// Number of ticks so far.
    [[nodiscard]] u64 frameCount() const noexcept { return m_frameCount; }

    /// Frames per second averaged over the last completed window (see the constructor); 0 until the first
    /// window completes. Changes at most once per window, so it reads well in a title or overlay.
    [[nodiscard]] f32 fps() const noexcept { return m_fps; }

private:
    std::chrono::nanoseconds m_fpsWindow;
    std::chrono::nanoseconds m_first{0};
    std::chrono::nanoseconds m_last{0};
    std::chrono::nanoseconds m_delta{0};
    u64 m_frameCount = 0;
    std::chrono::nanoseconds m_windowTime{0}; // time and frames of the window in progress
    u32 m_windowFrames = 0;
    f32 m_fps = 0.0F;
};

} // namespace teserra::platform
