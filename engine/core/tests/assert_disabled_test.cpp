// This translation unit disables TESERRA_ASSERT, so the compiled-out behavior is checked in every build
// configuration. Overriding the definition from teserra_core only for a test is fine: the macros are
// the only code that depends on it.
#undef TESERRA_ENABLE_ASSERTS
#define TESERRA_ENABLE_ASSERTS 0

#include <teserra/core/assert.hpp>
#include <teserra/test/assert.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("assert: when disabled, ASSERT evaluates nothing and VERIFY only its condition", "[core][assert]") {
    int evaluated = 0;

    const auto failure = teserra::test::captureAssert([&] {
        TESERRA_ASSERT(++evaluated < 0, "{}", ++evaluated);
        TESERRA_VERIFY(++evaluated < 0, "{}", ++evaluated);
    });

    CHECK_FALSE(failure);
    CHECK(evaluated == 1);
}
