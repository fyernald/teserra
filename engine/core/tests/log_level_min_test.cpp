// This translation unit raises the compile-time floor to Warn. TESERRA_LOG_LEVEL_MIN only affects the
// files compiled with it, so the check below holds in every build configuration.
#define TESERRA_LOG_LEVEL_MIN TESERRA_LOG_LEVEL_WARN

#include <teserra/core/log.hpp>

#include "log_capture.hpp"
#include <catch2/catch_test_macros.hpp>

using teserra::LogCategory;
using teserra::LogLevel;

TEST_CASE("log: calls below TESERRA_LOG_LEVEL_MIN are removed with their arguments", "[core][log]") {
    teserra::log::setLevel(LogLevel::Trace);
    teserra::test::LogCapture capture;
    int evaluated = 0;

    TESERRA_LOG_TRACE(LogCategory::Core, "{}", ++evaluated);
    TESERRA_LOG_DEBUG(LogCategory::Core, "{}", ++evaluated);
    TESERRA_LOG_INFO(LogCategory::Core, "{}", ++evaluated);
    TESERRA_LOG_WARN(LogCategory::Core, "{}", ++evaluated);

    teserra::log::setLevel(TESERRA_DEBUG_BUILD ? LogLevel::Debug : LogLevel::Info);

    CHECK(evaluated == 1);
    REQUIRE(capture.records().size() == 1);
    CHECK(capture.records()[0].level == LogLevel::Warn);
    CHECK(capture.records()[0].message == "1");
}
