#include <teserra/platform/fixed_step_accumulator.hpp>

#include <teserra/platform/clock.hpp>
#include <teserra/platform/frame_timer.hpp>
#include <teserra/test/assert.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <vector>

using namespace teserra;
using namespace teserra::platform;
using namespace std::chrono_literals;

namespace {

// Steps returned for each scripted frame delta.
std::vector<u32> stepsFor(FixedStepAccumulator& accumulator,
                          const std::vector<std::chrono::nanoseconds>& deltas) {
    std::vector<u32> steps;
    steps.reserve(deltas.size());
    for (const std::chrono::nanoseconds delta : deltas) {
        steps.push_back(accumulator.advance(delta));
    }
    return steps;
}

} // namespace

TEST_CASE("FixedStepAccumulator: defaults to 60 steps per second", "[platform][clock]") {
    const FixedStepAccumulator accumulator;

    CHECK(accumulator.step() == FixedStepAccumulator::kDefaultStep);
    CHECK(accumulator.stepSeconds() == Catch::Approx(1.0F / 60.0F));
    CHECK(accumulator.maxStepsPerFrame() == FixedStepAccumulator::kDefaultMaxStepsPerFrame);
}

TEST_CASE("FixedStepAccumulator: frames shorter than a step carry over", "[platform][clock]") {
    FixedStepAccumulator accumulator{10ms};

    CHECK(stepsFor(accumulator, {4ms, 4ms, 4ms, 4ms, 4ms}) == std::vector<u32>{0, 0, 1, 0, 1});
    CHECK(accumulator.accumulated() == 0ms);
}

TEST_CASE("FixedStepAccumulator: long frames run several steps and keep the remainder", "[platform][clock]") {
    FixedStepAccumulator accumulator{10ms};

    CHECK(accumulator.advance(25ms) == 2);
    CHECK(accumulator.accumulated() == 5ms);
    CHECK(accumulator.advance(5ms) == 1);
    CHECK(accumulator.accumulated() == 0ms);
    CHECK(accumulator.advance(30ms) == 3); // exact multiple
    CHECK(accumulator.accumulated() == 0ms);
    CHECK(accumulator.advance(0ms) == 0);
    CHECK(accumulator.lastDroppedSteps() == 0);
}

TEST_CASE("FixedStepAccumulator: a stall is clamped to maxStepsPerFrame", "[platform][clock]") {
    FixedStepAccumulator accumulator{10ms, 5};
    CHECK(accumulator.advance(3ms) == 0);

    // A 1 s stall (e.g. a breakpoint): 100 steps are due, only 5 run.
    CHECK(accumulator.advance(1s) == 5);
    CHECK(accumulator.lastDroppedSteps() == 95);
    CHECK(accumulator.accumulated() == 3ms); // the part below one step is kept

    // The next frame is normal again: no backlog.
    CHECK(accumulator.advance(16ms) == 1);
    CHECK(accumulator.lastDroppedSteps() == 0);
    CHECK(accumulator.accumulated() == 9ms);
}

TEST_CASE("FixedStepAccumulator: simulated time matches real time when nothing is dropped",
          "[platform][clock]") {
    ManualClock clock;
    FrameTimer timer;
    FixedStepAccumulator accumulator{FixedStepAccumulator::kDefaultStep};
    timer.tick(clock.now());

    // 10 s of frames with uneven times around 144 Hz, 60 Hz and 30 Hz.
    const std::vector<std::chrono::nanoseconds> frameTimes{6944us, 16667us, 33333us, 7ms, 21ms};
    u64 totalSteps = 0;
    while (timer.elapsed() < 10s) {
        clock.advance(frameTimes[timer.frameCount() % frameTimes.size()]);
        timer.tick(clock.now());
        totalSteps += accumulator.advance(timer.delta());
        REQUIRE(accumulator.lastDroppedSteps() == 0);
    }

    CHECK(static_cast<i64>(totalSteps) * accumulator.step() + accumulator.accumulated() == timer.elapsed());
    // About 10 s at 60 Hz; the loop overshoots 10 s by up to one frame.
    CHECK(totalSteps >= 599);
    CHECK(totalSteps <= 603);
}

#if TESERRA_ENABLE_ASSERTS
TEST_CASE("FixedStepAccumulator: invalid settings and negative deltas assert", "[platform][clock]") {
    REQUIRE_ASSERTS(FixedStepAccumulator{0ns});
    const auto noSteps = test::requireAssert([] { FixedStepAccumulator{10ms, 0}; });
    CHECK(noSteps.message() == "maxStepsPerFrame must be at least 1");

    FixedStepAccumulator accumulator{10ms};
    REQUIRE_ASSERTS(static_cast<void>(accumulator.advance(-1ms)));
}
#endif
