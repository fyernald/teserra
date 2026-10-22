#pragma once

// Time sources (docs/architecture/platform-abstraction.md#interfaces). Clock is an interface for
// testability, not portability: SteadyClock is plain std::chrono, and ManualClock scripts time in tests.

#include <chrono>

namespace teserra::platform {

/// A monotonic time source. Thread-safe.
class Clock {
public:
    virtual ~Clock() = default;

    /// Time since the clock's own start; it never decreases. Only differences between two calls are
    /// meaningful.
    [[nodiscard]] virtual std::chrono::nanoseconds now() const = 0;

protected:
    Clock() = default;
    Clock(const Clock&) = default;
    Clock& operator=(const Clock&) = default;
    Clock(Clock&&) = default;
    Clock& operator=(Clock&&) = default;
};

/// Real time from std::chrono::steady_clock, measured from the clock's construction.
class SteadyClock final : public Clock {
public:
    SteadyClock() noexcept;

    [[nodiscard]] std::chrono::nanoseconds now() const override;

private:
    std::chrono::steady_clock::time_point m_start;
};

/// Time that only moves when told to, for deterministic tests of anything driven by a Clock.
class ManualClock final : public Clock {
public:
    explicit ManualClock(std::chrono::nanoseconds start = std::chrono::nanoseconds{0}) noexcept
        : m_now(start) {}

    [[nodiscard]] std::chrono::nanoseconds now() const override { return m_now; }

    /// Moves time forward by `duration`, which must not be negative (asserted): clocks are monotonic.
    void advance(std::chrono::nanoseconds duration);

private:
    std::chrono::nanoseconds m_now;
};

} // namespace teserra::platform
