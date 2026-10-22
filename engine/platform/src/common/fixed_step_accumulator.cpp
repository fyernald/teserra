#include <teserra/platform/fixed_step_accumulator.hpp>

#include <teserra/core/assert.hpp>

namespace teserra::platform {

FixedStepAccumulator::FixedStepAccumulator(std::chrono::nanoseconds step, u32 maxStepsPerFrame)
    : m_step(step), m_maxStepsPerFrame(maxStepsPerFrame) {
    TESERRA_ASSERT(step.count() > 0, "The fixed step must be positive ({} ns)", step.count());
    TESERRA_ASSERT(maxStepsPerFrame >= 1, "maxStepsPerFrame must be at least 1");
}

u32 FixedStepAccumulator::advance(std::chrono::nanoseconds frameDelta) {
    TESERRA_ASSERT(frameDelta.count() >= 0, "Frame delta is negative ({} ns)", frameDelta.count());

    m_accumulated += frameDelta;
    const auto dueSteps = static_cast<u64>(m_accumulated / m_step);
    m_accumulated %= m_step; // the part below one step carries over, also when steps are dropped

    const u64 steps = dueSteps < m_maxStepsPerFrame ? dueSteps : m_maxStepsPerFrame;
    m_lastDroppedSteps = dueSteps - steps;
    return static_cast<u32>(steps);
}

f32 FixedStepAccumulator::stepSeconds() const noexcept {
    return std::chrono::duration<f32>(m_step).count();
}

} // namespace teserra::platform
