#pragma once

// Test helpers for engine assertions (docs/guides/testing-strategy.md). Every executable from
// teserra_add_test() installs throwingAssertHandler() for the whole run, so a failed TESERRA_ASSERT
// fails the current test with its message instead of aborting the test executable.
//
//   REQUIRE_ASSERTS(pool.get(staleHandle));
//
//   const auto failure = teserra::test::requireAssert([&] { pool.get(staleHandle); });
//   CHECK(failure.message() == "...");
//
//   CHECK_FALSE(teserra::test::captureAssert([&] { pool.get(liveHandle); }));
//
// The assertion travels as an exception, so the code under test must not have noexcept frames between
// the failing check and the test.

#include <teserra/core/assert.hpp>
#include <teserra/core/types.hpp>

#include <catch2/catch_test_macros.hpp>

#include <exception>
#include <optional>
#include <string>
#include <utility>

namespace teserra::test {

/// A failed engine assertion, thrown by throwingAssertHandler().
class AssertionFailure : public std::exception {
public:
    explicit AssertionFailure(const AssertInfo& info);

    [[nodiscard]] const char* what() const noexcept override { return m_description.c_str(); }

    [[nodiscard]] AssertKind kind() const noexcept { return m_kind; }
    [[nodiscard]] const std::string& expression() const noexcept { return m_expression; }
    [[nodiscard]] const std::string& message() const noexcept { return m_message; }
    [[nodiscard]] const std::string& file() const noexcept { return m_file; }
    [[nodiscard]] u32 line() const noexcept { return m_line; }

private:
    AssertKind m_kind;
    std::string m_expression;
    std::string m_message;
    std::string m_file;
    u32 m_line;
    std::string m_description; // describeAssert() of the failure
};

/// Assert handler that throws AssertionFailure.
[[noreturn]] void throwingAssertHandler(const AssertInfo& info);

/// Runs `function` and returns the assertion failure it raised, or std::nullopt if none.
template <typename Function>
[[nodiscard]] std::optional<AssertionFailure> captureAssert(Function&& function) {
    try {
        std::forward<Function>(function)();
    } catch (const AssertionFailure& failure) {
        return failure;
    }
    return std::nullopt;
}

/// Runs `function`, requires that it raised an assertion failure and returns it.
template <typename Function>
[[nodiscard]] AssertionFailure requireAssert(Function&& function) {
    try {
        std::forward<Function>(function)();
    } catch (const AssertionFailure& failure) {
        return failure;
    }
    FAIL("expected an engine assertion failure, but none was raised");
    std::terminate(); // not reached: FAIL ends the test case
}

} // namespace teserra::test

// NOLINTNEXTLINE(readability-identifier-naming): named like the Catch2 assertion macros it extends
#define REQUIRE_ASSERTS(...) REQUIRE_THROWS_AS(__VA_ARGS__, ::teserra::test::AssertionFailure)
