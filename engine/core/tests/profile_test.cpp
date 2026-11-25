#include <teserra/core/profile.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string_view>

#if defined(TESERRA_ENABLE_PROFILING) && TESERRA_ENABLE_PROFILING

namespace {

int instrumented(std::string_view system) {
    TESERRA_PROFILE_FUNCTION();
    {
        TESERRA_PROFILE_SCOPE("inner");
    }
    {
        TESERRA_PROFILE_SCOPE_DYNAMIC(system);
    }
    TESERRA_PROFILE_FRAME_MARK();
    return 42;
}

} // namespace

// Tracy only records while a viewer is connected (the on-demand feature), so zones without one are cheap
// no-ops at run time; this checks that every macro compiles and runs.
TEST_CASE("profile: macros record Tracy zones when profiling is on", "[core][profile]") {
    TESERRA_PROFILE_THREAD_NAME("Test");
    CHECK(instrumented("move_player") == 42);
    int block = 0;
    TESERRA_PROFILE_ALLOC(&block, sizeof(block));
    TESERRA_PROFILE_FREE(&block);
}

#else

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
    TESERRA_PROFILE_SCOPE_DYNAMIC((++evaluated, std::string_view("system")));
    TESERRA_PROFILE_ALLOC((++evaluated, &evaluated), sizeof(evaluated));
    TESERRA_PROFILE_FREE((++evaluated, &evaluated));
    TESERRA_PROFILE_THREAD_NAME((++evaluated, "worker"));

    CHECK(evaluated == 0);
}

#endif
