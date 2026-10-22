#include <teserra/core/assert.hpp>

#include <teserra/core/log.hpp>
#include <teserra/test/assert.hpp>
#include <teserra/test/log_capture.hpp>

#include <catch2/catch_test_macros.hpp>

#include <format>
#include <string>

using namespace teserra;

namespace {

int handlerCalls = 0;

void countingHandler(const AssertInfo& /*info*/) {
    ++handlerCalls;
}

// Installs `handler` for one test and puts the previous one (the test-run handler) back afterwards.
class ScopedAssertHandler {
public:
    explicit ScopedAssertHandler(AssertHandler handler) noexcept
        : m_previous(setAssertHandler(handler)) {}
    ~ScopedAssertHandler() { setAssertHandler(m_previous); }

    ScopedAssertHandler(const ScopedAssertHandler&) = delete;
    ScopedAssertHandler& operator=(const ScopedAssertHandler&) = delete;

private:
    AssertHandler m_previous;
};

#if TESERRA_ENABLE_ASSERTS
void requireNonNegative(int value) {
    TESERRA_ASSERT(value >= 0, "Expected a non-negative value, got {}", value);
}

void requireEven(int value) {
    TESERRA_ASSERT(value % 2 == 0);
}

// Values come in as parameters so that conditions are not constant expressions (MSVC warning C4127).
constexpr u32 kRequireBelowLine = __LINE__ + 2;
void requireBelow(int index, int size) {
    TESERRA_ASSERT(index < size, "Index {} out of range for '{}'", index, "quad");
}
#endif

} // namespace

// The expectation comes from the TESERRA_ENABLE_ASSERTS CMake option and the build configuration, so
// this cross-checks the compile definition on teserra_core.
TEST_CASE("assert: TESERRA_ENABLE_ASSERTS follows the build configuration", "[core][assert]") {
    STATIC_CHECK(TESERRA_ENABLE_ASSERTS == TESERRA_TEST_EXPECTED_ASSERTS);
}

TEST_CASE("assert: a passing check does nothing", "[core][assert]") {
    int evaluated = 0;
    TESERRA_ASSERT(++evaluated > 0);
    TESERRA_VERIFY(++evaluated > 0, "never formatted {}", evaluated);

    CHECK(evaluated == (TESERRA_ENABLE_ASSERTS ? 2 : 1));
}

#if TESERRA_ENABLE_ASSERTS
TEST_CASE("assert: a failure names the expression, file, line and message", "[core][assert]") {
    const auto failure = test::requireAssert([] { requireBelow(7, 3); });
    CHECK(failure.kind() == AssertKind::Assert);
    CHECK(failure.expression() == "index < size");
    CHECK(failure.message() == "Index 7 out of range for 'quad'");
    CHECK(failure.line() == kRequireBelowLine);
    CHECK(failure.file().ends_with("assert_test.cpp"));
    const std::string description = failure.what();
    CHECK(description.starts_with("Assertion failed: index < size ("));
    CHECK(description.ends_with(
        std::format("assert_test.cpp:{}): Index 7 out of range for 'quad'", kRequireBelowLine)));
}

TEST_CASE("assert: the message is optional", "[core][assert]") {
    const auto failure = test::requireAssert([] { requireEven(3); });
    CHECK(failure.expression() == "value % 2 == 0");
    CHECK(failure.message().empty());
    CHECK(std::string(failure.what()).ends_with(")"));
}

TEST_CASE("assert: a failure is logged at Critical with the caller's location", "[core][assert]") {
    // Even with Core turned off: failures bypass the level filters.
    log::setLevel(LogCategory::Core, LogLevel::Off);
    test::LogCapture capture;

    const auto failure = test::requireAssert([] { requireNonNegative(-1); });
    log::setLevel(LogCategory::Core, TESERRA_DEBUG_BUILD ? LogLevel::Debug : LogLevel::Info);
    REQUIRE(capture.records().size() == 1);
    const auto& record = capture.records().front();
    CHECK(record.level == LogLevel::Critical);
    CHECK(record.category == LogCategory::Core);
    CHECK(record.message.starts_with("Assertion failed: value >= 0 ("));
    CHECK(record.message.ends_with("Expected a non-negative value, got -1"));
    // The location of the check, not of the assertion machinery.
    CHECK(record.file.ends_with("assert_test.cpp"));
    CHECK(record.line == failure.line());
}
#endif

TEST_CASE("verify: the condition is evaluated once in every build", "[core][assert]") {
    int evaluated = 0;
    const auto failure =
        test::captureAssert([&] { TESERRA_VERIFY(++evaluated == 0, "evaluated {}", evaluated); });

    CHECK(evaluated == 1);
    CHECK(failure.has_value() == static_cast<bool>(TESERRA_ENABLE_ASSERTS));
    if (failure) {
        CHECK(failure->kind() == AssertKind::Verify);
        CHECK(std::string(failure->what()).starts_with("Verification failed: ++evaluated == 0 ("));
    }
}

#if TESERRA_ENABLE_ASSERTS
TEST_CASE("assert: a handler that returns lets execution continue", "[core][assert]") {
    const ScopedAssertHandler handler(&countingHandler);
    handlerCalls = 0;

    requireNonNegative(-1);
    requireNonNegative(-2);

    // Both calls returned after reporting.
    CHECK(handlerCalls == 2);
}
#endif

TEST_CASE("assert: setAssertHandler returns the previous handler", "[core][assert]") {
    const AssertHandler testRunHandler = setAssertHandler(&countingHandler);
    CHECK(testRunHandler == &test::throwingAssertHandler);

    // nullptr restores the default handler, which is a real function, not nullptr.
    CHECK(setAssertHandler(nullptr) == &countingHandler);
    const AssertHandler defaultHandler = setAssertHandler(testRunHandler);
    CHECK(defaultHandler != nullptr);
    CHECK(defaultHandler != &countingHandler);
}

TEST_CASE("fatal: logs at Critical and reports to the handler", "[core][assert]") {
    test::LogCapture capture;

    const auto failure = test::requireAssert([] { TESERRA_FATAL("GPU device lost: {}", "driver reset"); });
    CHECK(failure.kind() == AssertKind::Fatal);
    CHECK(failure.expression().empty());
    CHECK(failure.message() == "GPU device lost: driver reset");
    CHECK(std::string(failure.what()).starts_with("Fatal error ("));
    REQUIRE(capture.records().size() == 1);
    CHECK(capture.records()[0].level == LogLevel::Critical);
}

TEST_CASE("unreachable: is reported with or without a message", "[core][assert]") {
    const auto bare = test::requireAssert([] { TESERRA_UNREACHABLE(); });
    const auto described = test::requireAssert([] { TESERRA_UNREACHABLE("Unknown format {}", 42); });
    CHECK(bare.kind() == AssertKind::Unreachable);
    CHECK(std::string(bare.what()).starts_with("Unreachable code reached ("));
    CHECK(described.message() == "Unknown format 42");
}

#if TESERRA_ENABLE_ASSERTS
TEST_CASE("assert: REQUIRE_ASSERTS expects an assertion failure", "[core][assert]") {
    REQUIRE_ASSERTS(requireNonNegative(-5));
    CHECK_FALSE(test::captureAssert([] { requireNonNegative(5); }));
}
#endif
