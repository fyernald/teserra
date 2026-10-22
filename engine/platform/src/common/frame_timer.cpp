#include <teserra/platform/frame_timer.hpp>

#include <teserra/core/assert.hpp>

namespace teserra::platform {

FrameTimer::FrameTimer(std::chrono::nanoseconds fpsWindow)
    : m_fpsWindow(fpsWindow) {
    TESERRA_ASSERT(fpsWindow.count() > 0, "The FPS window must be positive ({} ns)", fpsWindow.count());
}

void FrameTimer::tick(std::chrono::nanoseconds now) {
    if (m_frameCount == 0) {
        m_first = now;
        m_last = now;
        m_frameCount = 1;
        return;
    }
    TESERRA_ASSERT(now >= m_last, "Frame time went backwards ({} ns before the previous tick)",
                   (m_last - now).count());

    m_delta = now - m_last;
    m_last = now;
    ++m_frameCount;

    m_windowTime += m_delta;
    ++m_windowFrames;
    if (m_windowTime >= m_fpsWindow) {
        m_fps = static_cast<f32>(static_cast<f64>(m_windowFrames) /
                                 std::chrono::duration<f64>(m_windowTime).count());
        m_windowTime = std::chrono::nanoseconds{0};
        m_windowFrames = 0;
    }
}

f32 FrameTimer::deltaSeconds() const noexcept {
    return std::chrono::duration<f32>(m_delta).count();
}

} // namespace teserra::platform
