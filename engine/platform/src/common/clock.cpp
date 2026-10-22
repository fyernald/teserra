#include <teserra/platform/clock.hpp>

#include <teserra/core/assert.hpp>

namespace teserra::platform {

SteadyClock::SteadyClock() noexcept
    : m_start(std::chrono::steady_clock::now()) {
}

std::chrono::nanoseconds SteadyClock::now() const {
    return std::chrono::steady_clock::now() - m_start;
}

void ManualClock::advance(std::chrono::nanoseconds duration) {
    TESERRA_ASSERT(duration.count() >= 0, "ManualClock cannot go back in time ({} ns)", duration.count());
    m_now += duration;
}

} // namespace teserra::platform
