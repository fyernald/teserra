#include <teserra/core/profile.hpp>

#include <catch2/catch_test_macros.hpp>

namespace {

// With profiling off the macros expand to nothing, so they are usable even in constant expressions.
constexpr int instrumented() {
    TESERRA_PROFILE_FUNCTION();
    TESERRA_PROFILE_SCOPE("inner");
    TESERRA_PROFILE_FRAME_MARK();
    return 42;
}

} // namespace

TEST_CASE("profile: macros compile to nothing when profiling is off", "[core][profile]") {
    STATIC_CHECK(instrumented() == 42);

    int evaluated = 0;
    TESERRA_PROFILE_SCOPE((++evaluated, "scope"));
    TESERRA_PROFILE_ALLOC((++evaluated, &evaluated), sizeof(evaluated));
    TESERRA_PROFILE_FREE((++evaluated, &evaluated));
    TESERRA_PROFILE_THREAD_NAME((++evaluated, "worker"));

    CHECK(evaluated == 0);
}
