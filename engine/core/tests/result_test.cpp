#include <teserra/core/result.hpp>

#include <teserra/core/log.hpp>
#include <teserra/test/log_capture.hpp>

#include <catch2/catch_test_macros.hpp>

#include <format>
#include <memory>
#include <string>

using namespace teserra;

namespace {

Result<int> parsePositive(int value) {
    if (value <= 0) {
        return fail(ErrorCode::InvalidArgument, Module::Core, "Expected a positive value, got {}", value);
    }
    return value;
}

Result<void> checkEven(int value) {
    if (value % 2 != 0) {
        return fail(ErrorCode::InvalidData, Module::Assets, "{} is odd", value);
    }
    return {};
}

// TESERRA_TRY_ASSIGN into a new constant and TESERRA_TRY on a Result<void>.
Result<int> doubledIfEven(int value) {
    TESERRA_TRY_ASSIGN(const int parsed, parsePositive(value));
    TESERRA_TRY(checkEven(parsed));
    return parsed * 2;
}

// TESERRA_TRY on a Result with a value discards it; TESERRA_TRY_ASSIGN into an existing variable.
Result<void> sumInto(int& total, int a, int b) {
    TESERRA_TRY(parsePositive(a));
    int parsed = 0;
    TESERRA_TRY_ASSIGN(parsed, parsePositive(b));
    total = a + parsed;
    return {};
}

Result<std::unique_ptr<int>> makeBox(int value) {
    TESERRA_TRY(parsePositive(value));
    return std::make_unique<int>(value);
}

Result<int> unbox(int value) {
    TESERRA_TRY_ASSIGN(const std::unique_ptr<int> box, makeBox(value));
    return *box;
}

} // namespace

TEST_CASE("Result: holds a value or an error", "[core][result]") {
    const Result<int> value = parsePositive(3);
    const Result<int> error = parsePositive(-1);

    REQUIRE(value.has_value());
    CHECK(*value == 3);
    REQUIRE_FALSE(error.has_value());
    CHECK(error.error() ==
          Error{ErrorCode::InvalidArgument, Module::Core, "Expected a positive value, got -1"});
}

TEST_CASE("Result: Result<void> succeeds with an empty return", "[core][result]") {
    CHECK(checkEven(4).has_value());

    const Result<void> odd = checkEven(3);
    REQUIRE_FALSE(odd.has_value());
    CHECK(odd.error().code == ErrorCode::InvalidData);
    CHECK(odd.error().module == Module::Assets);
}

TEST_CASE("TESERRA_TRY: propagates errors and unwraps values", "[core][result]") {
    CHECK(doubledIfEven(4) == Result<int>(8));

    const Result<int> notPositive = doubledIfEven(-2);
    REQUIRE_FALSE(notPositive.has_value());
    CHECK(notPositive.error().code == ErrorCode::InvalidArgument);

    const Result<int> odd = doubledIfEven(3);
    REQUIRE_FALSE(odd.has_value());
    CHECK(odd.error().message == "3 is odd");
}

TEST_CASE("TESERRA_TRY: works on value results, existing variables and move-only values", "[core][result]") {
    int total = 0;
    CHECK(sumInto(total, 2, 5).has_value());
    CHECK(total == 7);

    total = 0;
    CHECK_FALSE(sumInto(total, 2, -5).has_value());
    CHECK_FALSE(sumInto(total, -2, 5).has_value());
    CHECK(total == 0);

    CHECK(unbox(9) == Result<int>(9));
    CHECK(unbox(-9).error().code == ErrorCode::InvalidArgument);
}

TEST_CASE("TESERRA_TRY: evaluates the expression once", "[core][result]") {
    int calls = 0;
    const auto counted = [&calls]() -> Result<int> {
        ++calls;
        return 1;
    };
    const auto run = [&]() -> Result<int> {
        TESERRA_TRY(counted());
        TESERRA_TRY_ASSIGN(const int value, counted());
        return value;
    };

    CHECK(run() == Result<int>(1));
    CHECK(calls == 2);
}

TEST_CASE("Error: formats with message, module and code", "[core][result]") {
    const Error error{ErrorCode::NotFound, Module::Platform, "File 'hero.png' not found"};

    CHECK(std::format("{}", error) == "File 'hero.png' not found (Platform, NotFound)");
    CHECK(std::format("{}", Error{ErrorCode::Unsupported, Module::Rhi, ""}) == "Unsupported (Rhi)");
    CHECK(std::format("[{:>9}] [{:<8}]", ErrorCode::NotFound, Module::Rhi) == "[ NotFound] [Rhi     ]");
}

TEST_CASE("Error: formats in log messages", "[core][result]") {
    log::setLevel(LogCategory::Assets, LogLevel::Trace);
    test::LogCapture capture;
    const Result<int> result = parsePositive(0);

    TESERRA_LOG_ERROR(LogCategory::Assets, "Cannot load 'level1.json': {}", result.error());
    log::setLevel(LogCategory::Assets, TESERRA_DEBUG_BUILD ? LogLevel::Debug : LogLevel::Info);

    REQUIRE(capture.records().size() == 1);
    CHECK(capture.records()[0].message ==
          "Cannot load 'level1.json': Expected a positive value, got 0 (Core, InvalidArgument)");
}

TEST_CASE("Error: every code and module has a name", "[core][result]") {
    for (u8 value = 0; value <= static_cast<u8>(ErrorCode::Internal); ++value) {
        CHECK(errorCodeName(static_cast<ErrorCode>(value)) != "Unknown");
    }
    for (u8 value = 0; value <= static_cast<u8>(Module::Game); ++value) {
        CHECK(moduleName(static_cast<Module>(value)) != "Unknown");
    }
    STATIC_CHECK(errorCodeName(ErrorCode::CompilationFailed) == "CompilationFailed");
    STATIC_CHECK(moduleName(Module::Renderer) == "Renderer");
}
