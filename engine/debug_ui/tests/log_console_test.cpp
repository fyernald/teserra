// The log console (#299): the buffer keeps the last messages and drops the oldest; the console takes each
// message once, in order, also while other threads log; the level, category and text filters combine; and
// the console draws in an ImGui window.

#include <teserra/debug_ui/log_console.hpp>

#include <teserra/debug_ui/debug_ui.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <charconv>
#include <deque>
#include <memory>
#include <source_location>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

using namespace teserra;
using namespace teserra::debugui;

namespace {

log::LogRecord record(std::string_view message, LogLevel level = LogLevel::Info,
                      LogCategory category = LogCategory::Game) {
    return {.level = level,
            .category = category,
            .categoryName = log::categoryName(category),
            .message = message,
            .location = std::source_location::current()};
}

std::vector<std::string> messages(const std::deque<LogEntry>& entries) {
    std::vector<std::string> found;
    for (const LogEntry& entry : entries) {
        found.push_back(entry.message);
    }
    return found;
}

} // namespace

TEST_CASE("LogBuffer: keeps the last messages, drops the oldest, and copies only newer ones",
          "[debug_ui][log_console]") {
    LogBuffer buffer(3);
    for (const std::string_view message : {"one", "two", "three", "four", "five"}) {
        buffer.push(record(message));
    }

    std::deque<LogEntry> copied;
    u64 next = buffer.copyFrom(0, copied);
    CHECK(next == 5);
    CHECK(messages(copied) == std::vector<std::string>{"three", "four", "five"});
    CHECK(copied.front().sequence == 2);

    buffer.push(record("six"));
    copied.clear();
    next = buffer.copyFrom(next, copied);
    CHECK(next == 6);
    CHECK(messages(copied) == std::vector<std::string>{"six"});
}

TEST_CASE("LogFilter: level, category and text conditions combine", "[debug_ui][log_console]") {
    const LogEntry warning{
        .level = LogLevel::Warn, .category = LogCategory::Renderer, .message = "Slow frame"};
    const LogEntry info{.level = LogLevel::Info, .category = LogCategory::Game, .message = "Coin collected"};
    LogFilter filter;
    CHECK(filter.passes(warning));
    CHECK(filter.passes(info));

    filter.minimumLevel = LogLevel::Warn;
    CHECK(filter.passes(warning));
    CHECK_FALSE(filter.passes(info));

    filter.minimumLevel = LogLevel::Trace;
    filter.hiddenCategories.set(static_cast<usize>(LogCategory::Renderer));
    CHECK_FALSE(filter.passes(warning));
    CHECK(filter.passes(info));

    filter.hiddenCategories.reset();
    filter.text = "COIN";
    CHECK_FALSE(filter.passes(warning));
    CHECK(filter.passes(info));
    filter.text = "render"; // the category's name matches too
    CHECK(filter.passes(warning));

    filter.minimumLevel = LogLevel::Error;
    CHECK_FALSE(filter.passes(warning)); // all three must hold
}

TEST_CASE("LogConsole: messages logged from two threads arrive once each, in each thread's order",
          "[debug_ui][log_console]") {
    constexpr int kPerThread = 500;
    LogConsole console(4 * kPerThread);
    std::atomic<int> finished = 0;
    const auto logMany = [&](int thread) {
        for (int i = 0; i < kPerThread; ++i) {
            TESERRA_LOG_WARN(LogCategory::Game, "console test {} {}", thread, i);
        }
        ++finished;
    };
    std::thread first(logMany, 0);
    std::thread second(logMany, 1);
    while (finished.load() < 2) {
        console.update(); // reads while the threads write
    }
    first.join();
    second.join();
    console.update();

    std::vector<int> next(2, 0);
    for (const LogEntry& entry : console.entries()) {
        // "console test <thread> <index>"
        constexpr std::string_view kPrefix = "console test ";
        if (!entry.message.starts_with(kPrefix)) {
            continue;
        }
        const char* const end = entry.message.data() + entry.message.size();
        int thread = 0;
        int index = 0;
        const auto parsedThread = std::from_chars(entry.message.data() + kPrefix.size(), end, thread);
        REQUIRE(parsedThread.ec == std::errc{});
        REQUIRE(std::from_chars(parsedThread.ptr + 1, end, index).ec == std::errc{});
        REQUIRE((thread == 0 || thread == 1));
        CHECK(index == next[static_cast<usize>(thread)]);
        next[static_cast<usize>(thread)] = index + 1;
    }
    CHECK(next == std::vector<int>{kPerThread, kPerThread});
}

TEST_CASE("LogConsole: drops the oldest past its capacity, and Clear keeps newer messages coming",
          "[debug_ui][log_console]") {
    LogConsole console(2);
    TESERRA_LOG_WARN(LogCategory::Game, "console a");
    TESERRA_LOG_WARN(LogCategory::Game, "console b");
    TESERRA_LOG_WARN(LogCategory::Game, "console c");
    console.update();
    CHECK(messages(console.entries()) == std::vector<std::string>{"console b", "console c"});

    console.clear();
    console.update();
    CHECK(console.entries().empty());
    TESERRA_LOG_WARN(LogCategory::Game, "console d");
    console.update();
    CHECK(messages(console.entries()) == std::vector<std::string>{"console d"});
}

TEST_CASE("LogConsole: draws its messages in an ImGui window", "[debug_ui][log_console]") {
    const std::unique_ptr<DebugUi> debugUi = *DebugUi::create();
    LogConsole console;
    debugUi->addWindow("Log", [&] { console.draw(); });
    TESERRA_LOG_WARN(LogCategory::Game, "console drawn");
    for (int frame = 0; frame < 2; ++frame) {
        debugUi->beginFrame({800.0F, 600.0F}, Vec2{1.0F}, 1.0F / 60.0F);
        debugUi->drawWindows();
        debugUi->endFrame();
    }
    REQUIRE_FALSE(console.entries().empty());
    CHECK(console.entries().back().message == "console drawn");
    CHECK(console.entries().back().level == LogLevel::Warn);
    CHECK(debugUi->drawData()->TotalVtxCount > 0);
}
