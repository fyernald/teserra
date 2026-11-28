#include <teserra/core/linear_arena.hpp>

#include <teserra/core/types.hpp>
#include <teserra/test/allocation_counter.hpp>
#include <teserra/test/assert.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <numeric>
#include <type_traits>
#include <vector>

using namespace teserra;

namespace {

// An upstream resource that counts what the arena takes from it and gives back.
class CountingResource final : public std::pmr::memory_resource {
public:
    usize allocations = 0;
    usize deallocations = 0;
    usize bytesHeld = 0;

private:
    void* do_allocate(usize bytes, usize alignment) override {
        ++allocations;
        bytesHeld += bytes;
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }
    void do_deallocate(void* pointer, usize bytes, usize alignment) override {
        ++deallocations;
        bytesHeld -= bytes;
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }
    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }
};

std::uintptr_t address(const void* pointer) {
    return reinterpret_cast<std::uintptr_t>(pointer); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
}

// A frame's transient work: per-cell lists of ids, the way a grid broadphase builds them.
u32 buildCells(std::pmr::memory_resource& memory, usize cellCount, usize idCount) {
    std::pmr::vector<std::pmr::vector<u32>> cells(cellCount, &memory);
    for (u32 id = 0; id < idCount; ++id) {
        cells[(static_cast<usize>(id) * 7U) % cellCount].push_back(id);
    }
    u32 sum = 0;
    for (const auto& cell : cells) {
        sum = std::accumulate(cell.begin(), cell.end(), sum);
    }
    return sum;
}

} // namespace

static_assert(!std::is_copy_constructible_v<LinearArena>);
static_assert(!std::is_move_constructible_v<LinearArena>);

TEST_CASE("LinearArena: allocations are aligned, distinct and counted in used()", "[core][arena]") {
    LinearArena arena(1024);
    CHECK(arena.capacity() == 0);
    CHECK(arena.blockCount() == 0);

    void* const a = arena.allocate(3, 1);
    void* const b = arena.allocate(8, 8);
    void* const c = arena.allocate(16, 16);
    void* const empty = arena.allocate(0, 1);

    CHECK(address(b) % 8 == 0);
    CHECK(address(c) % 16 == 0);
    CHECK(address(a) < address(b));
    CHECK(address(b) + 8 <= address(c));
    CHECK(address(c) + 16 <= address(empty));
    CHECK(arena.used() >= 3 + 8 + 16 + 1);
    CHECK(arena.used() <= 3 + 8 + 16 + 1 + 7 + 15);
    CHECK(arena.blockCount() == 1);
    CHECK(arena.capacity() == 1024);
    CHECK(arena.is_equal(arena));
    CHECK_FALSE(arena.is_equal(*std::pmr::new_delete_resource()));
}

TEST_CASE("LinearArena: over-aligned and oversized allocations", "[core][arena]") {
    LinearArena arena(256);

    void* const page = arena.allocate(100, 4096);
    CHECK(address(page) % 4096 == 0);

    void* const big = arena.allocate(10'000, 64);
    CHECK(address(big) % 64 == 0);
    CHECK(arena.capacity() >= 10'000);

    // Both can be written in full.
    std::fill_n(static_cast<std::byte*>(page), 100, std::byte{1});
    std::fill_n(static_cast<std::byte*>(big), 10'000, std::byte{2});
}

TEST_CASE("LinearArena: reset() hands out the same memory again", "[core][arena]") {
    LinearArena arena(1024);
    void* const first = arena.allocate(64, 16);
    arena.deallocate(first, 64, 16);
    CHECK(arena.used() == 64); // deallocation gives nothing back before reset()

    arena.reset();
    CHECK(arena.used() == 0);
    CHECK(arena.capacity() == 1024);
    CHECK(arena.allocate(64, 16) == first);
}

TEST_CASE("LinearArena: reset() merges the blocks of a frame that outgrew one", "[core][arena]") {
    CountingResource upstream;
    {
        LinearArena arena(1000, &upstream);
        for (int i = 0; i < 5; ++i) {
            static_cast<void>(arena.allocate(600, 8)); // one block each
        }
        CHECK(arena.blockCount() == 5);
        CHECK(upstream.allocations == 5);

        arena.reset();
        CHECK(arena.blockCount() == 1);
        CHECK(arena.capacity() == 5000);
        CHECK(upstream.allocations == 6);
        CHECK(upstream.deallocations == 5);

        // The same frame again fits in the merged block.
        for (int frame = 0; frame < 3; ++frame) {
            for (int i = 0; i < 5; ++i) {
                static_cast<void>(arena.allocate(600, 8));
            }
            arena.reset();
        }
        CHECK(upstream.allocations == 6);
        CHECK(arena.blockCount() == 1);
    }
    CHECK(upstream.bytesHeld == 0);
    CHECK(upstream.allocations == upstream.deallocations);
}

TEST_CASE("LinearArena: release() returns all memory upstream", "[core][arena]") {
    CountingResource upstream;
    LinearArena arena(512, &upstream);
    static_cast<void>(arena.allocate(100, 8));
    static_cast<void>(arena.allocate(1000, 8));

    arena.release();
    CHECK(upstream.bytesHeld == 0);
    CHECK(arena.capacity() == 0);
    CHECK(arena.used() == 0);

    static_cast<void>(arena.allocate(8, 8)); // usable again
    CHECK(arena.blockCount() == 1);
}

TEST_CASE("LinearArena: pmr containers on it allocate nothing after warm-up", "[core][arena]") {
    LinearArena arena(4096);

    // Warm-up: the frame outgrows the first block, so reset() merges the blocks.
    const u32 expected = buildCells(arena, 64, 5000);
    CHECK(arena.blockCount() > 1);
    arena.reset();
    // The merged block may still be short when padding falls differently; one more frame settles it.
    CHECK(buildCells(arena, 64, 5000) == expected);
    arena.reset();

    const test::AllocationCounter allocations;
    for (int frame = 0; frame < 10; ++frame) {
        CHECK(buildCells(arena, 64, 5000) == expected);
        arena.reset();
    }
    CHECK(allocations.count() == 0);
}

#if TESERRA_ENABLE_ASSERTS

TEST_CASE("LinearArena: debug builds poison memory taken back by reset()", "[core][arena]") {
    LinearArena arena(256);
    auto* const bytes = static_cast<std::byte*>(arena.allocate(32, 1));
    std::fill_n(bytes, 32, std::byte{0x11});

    arena.reset();
    auto* const again = static_cast<std::byte*>(arena.allocate(32, 1));
    REQUIRE(again == bytes);
    CHECK(std::all_of(again, again + 32, [](std::byte value) { return value == LinearArena::kPoison; }));
}

TEST_CASE("LinearArena: debug builds catch a deallocation from before the last reset", "[core][arena]") {
    LinearArena arena(256);
    void* const stale = arena.allocate(32, 8);
    arena.reset();

    const auto failure = test::requireAssert([&] { arena.deallocate(stale, 32, 8); });
    CHECK(failure.message().find("since the last reset") != std::string::npos);

    // The same pointer is live again once it has been handed out again.
    void* const fresh = arena.allocate(32, 8);
    REQUIRE(fresh == stale);
    CHECK_FALSE(test::captureAssert([&] { arena.deallocate(fresh, 32, 8); }));
}

TEST_CASE("LinearArena: debug builds catch memory it never handed out", "[core][arena]") {
    LinearArena arena(256);
    static_cast<void>(arena.allocate(16, 8));
    int local = 0;
    REQUIRE_ASSERTS(arena.deallocate(&local, sizeof(local), alignof(int)));
}

#endif
