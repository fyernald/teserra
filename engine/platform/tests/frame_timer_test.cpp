#include <teserra/platform/frame_timer.hpp>

#include <teserra/platform/clock.hpp>
#include <teserra/test/assert.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>

using namespace teserra;
using namespace teserra::platform;
using namespace std::chrono_literals;

TEST_CASE("FrameTimer: the first tick has no delta", "[platform][clock]") {
    const ManualClock clock{10s};
    FrameTimer timer;
    CHECK(timer.frameCount() == 0);

    timer.tick(clock.now());

    CHECK(timer.frameCount() == 1);
    CHECK(timer.delta() == 0ns);
    CHECK(timer.deltaSeconds() == 0.0F);
    CHECK(timer.elapsed() == 0ns);
}

TEST_CASE("FrameTimer: delta is the time between ticks, elapsed the time since the first",
          "[platform][clock]") {
    ManualClock clock;
    FrameTimer timer;
    timer.tick(clock.now());

    clock.advance(16ms);
    timer.tick(clock.now());
    CHECK(timer.delta() == 16ms);
    CHECK(timer.deltaSeconds() == Catch::Approx(0.016F));

    clock.advance(40ms);
    timer.tick(clock.now());
    CHECK(timer.delta() == 40ms);
    CHECK(timer.elapsed() == 56ms);
    CHECK(timer.frameCount() == 3);

    timer.tick(clock.now()); // no time passed
    CHECK(timer.delta() == 0ns);
}

TEST_CASE("FrameTimer: fps is averaged over completed windows", "[platform][clock]") {
    ManualClock clock;
    FrameTimer timer{500ms};
    timer.tick(clock.now());

    // 24 frames of 20 ms: 480 ms, the window is not complete yet.
    for (int i = 0; i < 24; ++i) {
        clock.advance(20ms);
        timer.tick(clock.now());
    }
    CHECK(timer.fps() == 0.0F);

    // The 25th frame completes 500 ms: 25 frames / 0.5 s.
    clock.advance(20ms);
    timer.tick(clock.now());
    CHECK(timer.fps() == Catch::Approx(50.0F));

    // A slower window: 5 frames of 100 ms. The value only changes once it completes.
    for (int i = 0; i < 4; ++i) {
        clock.advance(100ms);
        timer.tick(clock.now());
        CHECK(timer.fps() == Catch::Approx(50.0F));
    }
    clock.advance(100ms);
    timer.tick(clock.now());
    CHECK(timer.fps() == Catch::Approx(10.0F));
}

TEST_CASE("FrameTimer: one long frame completes a window on its own", "[platform][clock]") {
    ManualClock clock;
    FrameTimer timer{500ms};
    timer.tick(clock.now());

    clock.advance(2s);
    timer.tick(clock.now());

    CHECK(timer.fps() == Catch::Approx(0.5F));
}

#if TESERRA_ENABLE_ASSERTS
TEST_CASE("FrameTimer: time going backwards asserts", "[platform][clock]") {
    FrameTimer timer;
    timer.tick(100ms);

    const auto failure = test::requireAssert([&] { timer.tick(90ms); });

    CHECK(failure.message() == "Frame time went backwards (10000000 ns before the previous tick)");
}

TEST_CASE("FrameTimer: the FPS window must be positive", "[platform][clock]") {
    REQUIRE_ASSERTS(FrameTimer{0ns});
}
#endif
