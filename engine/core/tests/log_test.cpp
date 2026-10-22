#include <teserra/core/log.hpp>

#include "log_capture.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <source_location>
#include <string>
#include <utility>

using namespace teserra;

namespace {

// Tests observe logging through callback sinks; console output would only add noise to test reports.
class QuietLogListener final : public Catch::EventListenerBase {
public:
    using Catch::EventListenerBase::EventListenerBase;

    void testRunStarting(const Catch::TestRunInfo& /*testRunInfo*/) override {
        log::LogConfig config;
        config.console = false;
        static_cast<void>(log::configure(config));
    }
};

// Sets the runtime level of every category for one test and restores the build's default afterwards.
class ScopedLevel {
public:
    explicit ScopedLevel(LogLevel level) noexcept { log::setLevel(level); }
    ~ScopedLevel() { log::setLevel(TESERRA_DEBUG_BUILD ? LogLevel::Debug : LogLevel::Info); }

    ScopedLevel(const ScopedLevel&) = delete;
    ScopedLevel& operator=(const ScopedLevel&) = delete;
};

struct Extent {
    int width;
    int height;
};

} // namespace

CATCH_REGISTER_LISTENER(QuietLogListener)

template <>
struct std::formatter<Extent> : std::formatter<std::string_view> {
    auto format(const Extent& extent, std::format_context& context) const {
        return std::format_to(context.out(), "{}x{}", extent.width, extent.height);
    }
};

TEST_CASE("log: callback sinks receive level, category, message and location", "[core][log]") {
    const ScopedLevel level(LogLevel::Trace);
    test::LogCapture capture;

    const u32 line = std::source_location::current().line() + 1;
    TESERRA_LOG_INFO(LogCategory::Renderer, "Created swapchain {}x{}", 800, 600);

    REQUIRE(capture.records().size() == 1);
    const auto& record = capture.records().front();
    CHECK(record.level == LogLevel::Info);
    CHECK(record.category == LogCategory::Renderer);
    CHECK(record.categoryName == "Renderer");
    CHECK(record.message == "Created swapchain 800x600");
    CHECK(record.line == line);
    CHECK(record.file.ends_with("log_test.cpp"));
}

TEST_CASE("log: every level macro logs at its level", "[core][log]") {
    const ScopedLevel level(LogLevel::Trace);
    test::LogCapture capture;

    // Trace and Debug are covered by the compile-time floor test below: they are removed in Release.
    TESERRA_LOG_INFO(LogCategory::Core, "info");
    TESERRA_LOG_WARN(LogCategory::Core, "warn");
    TESERRA_LOG_ERROR(LogCategory::Core, "error");
    TESERRA_LOG_CRITICAL(LogCategory::Core, "critical");

    REQUIRE(capture.records().size() == 4);
    CHECK(capture.records()[0].level == LogLevel::Info);
    CHECK(capture.records()[1].level == LogLevel::Warn);
    CHECK(capture.records()[2].level == LogLevel::Error);
    CHECK(capture.records()[3].level == LogLevel::Critical);
}

TEST_CASE("log: compile-time floor defaults to Trace in Debug and Info otherwise", "[core][log]") {
#if TESERRA_DEBUG_BUILD
    STATIC_CHECK(TESERRA_LOG_LEVEL_MIN == TESERRA_LOG_LEVEL_TRACE);
#else
    STATIC_CHECK(TESERRA_LOG_LEVEL_MIN == TESERRA_LOG_LEVEL_INFO);
#endif
}

// In Release, Trace and Debug calls are removed at compile time: the arguments are never evaluated, even
// though the runtime level would let the messages through.
TEST_CASE("log: Trace and Debug calls are compiled out in Release builds", "[core][log]") {
    const ScopedLevel level(LogLevel::Trace);
    test::LogCapture capture;
    int evaluated = 0;

    TESERRA_LOG_TRACE(LogCategory::Core, "frame {}", ++evaluated);
    TESERRA_LOG_DEBUG(LogCategory::Core, "frame {}", ++evaluated);

    CHECK(evaluated == (TESERRA_DEBUG_BUILD ? 2 : 0));
    REQUIRE(capture.records().size() == (TESERRA_DEBUG_BUILD ? 2U : 0U));
#if TESERRA_DEBUG_BUILD
    CHECK(capture.records()[0].level == LogLevel::Trace);
    CHECK(capture.records()[1].level == LogLevel::Debug);
#endif
}

TEST_CASE("log: runtime level filters per category before formatting", "[core][log]") {
    const ScopedLevel level(LogLevel::Trace);
    log::setLevel(LogCategory::Rhi, LogLevel::Warn);
    test::LogCapture capture;
    int evaluated = 0;

    TESERRA_LOG_INFO(LogCategory::Rhi, "{}", ++evaluated);
    TESERRA_LOG_INFO(LogCategory::Renderer, "{}", ++evaluated);
    TESERRA_LOG_WARN(LogCategory::Rhi, "{}", ++evaluated);

    CHECK(log::level(LogCategory::Rhi) == LogLevel::Warn);
    CHECK(log::level(LogCategory::Renderer) == LogLevel::Trace);
    CHECK_FALSE(log::isEnabled(LogCategory::Rhi, LogLevel::Info));
    CHECK(log::isEnabled(LogCategory::Rhi, LogLevel::Error));
    // The filtered call did not evaluate its argument.
    CHECK(evaluated == 2);
    REQUIRE(capture.records().size() == 2);
    CHECK(capture.records()[0].category == LogCategory::Renderer);
    CHECK(capture.records()[1].category == LogCategory::Rhi);
}

TEST_CASE("log: Off disables a category", "[core][log]") {
    const ScopedLevel level(LogLevel::Trace);
    log::setLevel(LogCategory::Assets, LogLevel::Off);
    test::LogCapture capture;

    TESERRA_LOG_CRITICAL(LogCategory::Assets, "dropped");

    CHECK(capture.records().empty());
    CHECK_FALSE(log::isEnabled(LogCategory::Core, LogLevel::Off));
}

TEST_CASE("log: registered categories are stable, named and filtered", "[core][log]") {
    const ScopedLevel level(LogLevel::Info);
    const LogCategory ai = log::registerCategory("AI");

    CHECK(log::registerCategory("AI") == ai);
    CHECK(log::registerCategory("Renderer") == LogCategory::Renderer);
    CHECK(log::categoryName(ai) == "AI");
    CHECK(log::categoryName(LogCategory::Ecs) == "Ecs");
    CHECK(log::categoryName(static_cast<LogCategory>(log::kMaxCategories - 1)) == "Unknown");

    test::LogCapture capture;
    log::setLevel(ai, LogLevel::Error);
    TESERRA_LOG_WARN(ai, "filtered");
    TESERRA_LOG_ERROR(ai, "Path not found");

    REQUIRE(capture.records().size() == 1);
    CHECK(capture.records()[0].categoryName == "AI");
    CHECK(capture.records()[0].message == "Path not found");
}

TEST_CASE("log: setLevel without a category also applies to categories registered later", "[core][log]") {
    const ScopedLevel level(LogLevel::Error);
    const LogCategory later = log::registerCategory("RegisteredAfterSetLevel");

    CHECK(log::level(later) == LogLevel::Error);
    CHECK(log::level(LogCategory::Game) == LogLevel::Error);
}

TEST_CASE("log: invalid category names fall back to Game with a warning", "[core][log]") {
    const ScopedLevel level(LogLevel::Trace);
    test::LogCapture capture;

    CHECK(log::registerCategory("") == LogCategory::Game);
    CHECK(log::registerCategory(std::string(log::kMaxCategoryNameLength + 1, 'n')) == LogCategory::Game);
    CHECK(log::categoryName(log::registerCategory(std::string(log::kMaxCategoryNameLength, 'n'))) ==
          std::string(log::kMaxCategoryNameLength, 'n'));

    REQUIRE(capture.records().size() == 2);
    CHECK(capture.records()[0].level == LogLevel::Warn);
    CHECK(capture.records()[0].category == LogCategory::Core);
}

TEST_CASE("log: messages longer than the stack buffer are complete", "[core][log]") {
    const ScopedLevel level(LogLevel::Trace);
    test::LogCapture capture;
    const std::string text(5000, 'x');

    TESERRA_LOG_ERROR(LogCategory::Rhi, "Shader log: {}!", text);

    REQUIRE(capture.records().size() == 1);
    CHECK(capture.records()[0].message == "Shader log: " + text + "!");
}

TEST_CASE("log: types with a std::formatter can be logged", "[core][log]") {
    const ScopedLevel level(LogLevel::Trace);
    test::LogCapture capture;

    TESERRA_LOG_INFO(LogCategory::Platform, "Window resized to {}", Extent{1280, 720});

    REQUIRE(capture.records().size() == 1);
    CHECK(capture.records()[0].message == "Window resized to 1280x720");
}

TEST_CASE("log: callback sinks stop receiving when destroyed and follow moves", "[core][log]") {
    const ScopedLevel level(LogLevel::Trace);
    int received = 0;
    {
        log::CallbackSink sink([&received](const log::LogRecord& /*record*/) { ++received; });
        TESERRA_LOG_INFO(LogCategory::Core, "one");

        log::CallbackSink moved(std::move(sink));
        TESERRA_LOG_INFO(LogCategory::Core, "two");

        log::CallbackSink assigned([](const log::LogRecord& /*record*/) {});
        assigned = std::move(moved);
        TESERRA_LOG_INFO(LogCategory::Core, "three");
    }
    TESERRA_LOG_INFO(LogCategory::Core, "four");

    // One subscription throughout: no duplicates from the moves, nothing after destruction.
    CHECK(received == 3);
}

TEST_CASE("log: messages logged from a callback sink are dropped", "[core][log]") {
    const ScopedLevel level(LogLevel::Trace);
    int received = 0;
    const log::CallbackSink sink([&received](const log::LogRecord& /*record*/) {
        ++received;
        TESERRA_LOG_ERROR(LogCategory::Core, "logged from a callback");
    });

    TESERRA_LOG_INFO(LogCategory::Core, "outer");

    CHECK(received == 1);
}

TEST_CASE("log: configure writes messages to a rotating log file", "[core][log]") {
    const ScopedLevel level(LogLevel::Trace);
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        std::format("teserra_log_test_{}", std::chrono::steady_clock::now().time_since_epoch().count());
    const std::filesystem::path file = directory / "teserra.log";

    log::LogConfig config;
    config.console = false;
    config.filePath = file;
    REQUIRE(log::configure(config));

    TESERRA_LOG_INFO(LogCategory::Renderer, "Created swapchain {}x{}", 800, 600);
    log::flush();

    // Closes the file so it can be read and deleted.
    config.filePath.clear();
    REQUIRE(log::configure(config));

    std::ifstream stream(file);
    const std::string contents{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    stream.close();
    std::filesystem::remove_all(directory);

    CHECK(contents.find("[Renderer] [thread ") != std::string::npos);
    CHECK(contents.find("Created swapchain 800x600 (log_test.cpp:") != std::string::npos);
}

TEST_CASE("log: configure reports a log file that cannot be opened", "[core][log]") {
    const ScopedLevel level(LogLevel::Trace);
    test::LogCapture capture;

    // A directory cannot be opened as a file.
    log::LogConfig config;
    config.console = false;
    config.filePath = std::filesystem::temp_directory_path();
    CHECK_FALSE(log::configure(config));

    REQUIRE(capture.records().size() == 1);
    CHECK(capture.records()[0].level == LogLevel::Error);
    CHECK(capture.records()[0].message.starts_with("Cannot open log file"));
}
