#pragma once

#include <teserra/core/types.hpp>

#include <chrono>

namespace teserra::platform {

/// Turns variable frame times into a whole number of fixed simulation steps (docs/architecture/
/// overview.md#frame-flow-v1):
///
///     const u32 steps = fixedStep.advance(timer.delta());
///     for (u32 i = 0; i < steps; ++i) {
///         fixedUpdate(fixedStep.stepSeconds());
///     }
///
/// Time below one step carries over to the next frame. After a stall (a breakpoint, a window drag, a
/// slow frame), at most maxStepsPerFrame steps run and the rest are dropped, so a slow simulation does not
/// fall further behind every frame (the "spiral of death"). A plain value.
class FixedStepAccumulator {
public:
    /// The default step for 60 updates per second.
    static constexpr std::chrono::nanoseconds kDefaultStep{16'666'667};
    static constexpr u32 kDefaultMaxStepsPerFrame = 5;

    /// `step` must be positive and `maxStepsPerFrame` at least 1 (asserted).
    explicit FixedStepAccumulator(std::chrono::nanoseconds step = kDefaultStep,
                                  u32 maxStepsPerFrame = kDefaultMaxStepsPerFrame);

    /// Adds a frame's time (not negative, asserted) and returns how many steps to run now, at most
    /// maxStepsPerFrame().
    [[nodiscard]] u32 advance(std::chrono::nanoseconds frameDelta);

    /// Steps the last advance() dropped because of the limit; non-zero means the simulation fell behind.
    [[nodiscard]] u64 lastDroppedSteps() const noexcept { return m_lastDroppedSteps; }

    [[nodiscard]] std::chrono::nanoseconds step() const noexcept { return m_step; }
    [[nodiscard]] f32 stepSeconds() const noexcept;
    [[nodiscard]] u32 maxStepsPerFrame() const noexcept { return m_maxStepsPerFrame; }

    /// Time carried over to the next frame; always less than one step.
    [[nodiscard]] std::chrono::nanoseconds accumulated() const noexcept { return m_accumulated; }

private:
    std::chrono::nanoseconds m_step;
    u32 m_maxStepsPerFrame;
    std::chrono::nanoseconds m_accumulated{0};
    u64 m_lastDroppedSteps = 0;
};

} // namespace teserra::platform
