// The allocation counter of the test support library (<teserra/test/allocation_counter.hpp>), which the
// steady-state allocation tests of other modules rely on.

#include <teserra/test/allocation_counter.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <new>
#include <thread>
#include <vector>

using namespace teserra;

namespace {

struct alignas(64) OverAligned {
    u8 bytes[64]; // NOLINT(modernize-avoid-c-arrays): only its size and alignment matter
};

} // namespace

TEST_CASE("AllocationCounter: counts the allocations of the current thread", "[test_support][allocation]") {
    u64 plain = 0;
    u64 array = 0;
    u64 aligned = 0;
    u64 none = 0;
    {
        const test::AllocationCounter counter;
        const auto value = std::make_unique<int>(1);
        plain = counter.count();
        const auto values = std::make_unique<int[]>(8); // NOLINT(modernize-avoid-c-arrays): tests new[]
        array = counter.count() - plain;
        const auto overAligned = std::make_unique<OverAligned>();
        aligned = counter.count() - plain - array;
        CHECK(reinterpret_cast<std::uintptr_t>(overAligned.get()) % 64 == 0);
    }
    {
        const test::AllocationCounter counter;
        const std::array values{1, 2, 3};
        none = counter.count();
        CHECK(values[1] == 2);
    }

    CHECK(plain == 1);
    CHECK(array == 1);
    CHECK(aligned == 1);
    CHECK(none == 0);
}

TEST_CASE("AllocationCounter: ignores other threads", "[test_support][allocation]") {
    u64 counted = 0;
    {
        const test::AllocationCounter counter;
        std::thread worker([] {
            const std::vector<int> values(1000, 1);
            static_cast<void>(values);
        });
        const u64 beforeJoin = counter.count();
        worker.join();
        counted = counter.count() - beforeJoin;
    }

    CHECK(counted == 0);
}
