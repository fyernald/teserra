#include <teserra/platform/clock.hpp>

#include <teserra/platform/platform.hpp>
#include <teserra/test/assert.hpp>

#include "platform_test_support.hpp"
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>

using namespace teserra;
using namespace teserra::platform;
using namespace std::chrono_literals;

TEST_CASE("SteadyClock: starts near zero, never decreases and follows real time", "[platform][clock]") {
    const SteadyClock clock;

    const std::chrono::nanoseconds first = clock.now();
    CHECK(first >= 0ns);
    CHECK(first < 1s);

    std::this_thread::sleep_for(2ms);
    const std::chrono::nanoseconds second = clock.now();
    CHECK(second - first >= 2ms);

    std::chrono::nanoseconds previous = second;
    for (int i = 0; i < 1000; ++i) {
        const std::chrono::nanoseconds now = clock.now();
        REQUIRE(now >= previous);
        previous = now;
    }
}

TEST_CASE("ManualClock: moves only when advanced", "[platform][clock]") {
    ManualClock clock{5s};
    CHECK(clock.now() == 5s);
    CHECK(clock.now() == 5s);

    clock.advance(16ms);
    clock.advance(0ns);
    CHECK(clock.now() == 5s + 16ms);

    const Clock& asInterface = clock;
    CHECK(asInterface.now() == 5016ms);
}

#if TESERRA_ENABLE_ASSERTS
TEST_CASE("ManualClock: cannot go back in time", "[platform][clock]") {
    ManualClock clock;

    const auto failure = test::requireAssert([&] { clock.advance(-1ms); });

    CHECK(failure.message() == "ManualClock cannot go back in time (-1000000 ns)");
    CHECK(clock.now() == 0ns);
}
#endif

TEST_CASE("Platform: clock() is a running monotonic clock", "[platform][clock][window]") {
    const auto platform = test::createPlatformOrSkip();
    const Clock& clock = platform->clock();

    const std::chrono::nanoseconds first = clock.now();
    std::this_thread::sleep_for(1ms);

    CHECK(clock.now() - first >= 1ms);
}
