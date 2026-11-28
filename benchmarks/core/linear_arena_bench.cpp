// LinearArena micro-benchmarks (#136, docs/architecture/memory-and-performance.md#allocation-strategy): the
// transient containers of one frame, built four ways. Run the Release build: `teserra_core_bench "[arena]"`.
//
// - fresh std::vector: new containers every frame, the way gameplay code usually writes them; allocates.
// - reused std::vector: containers kept between frames and cleared (the engine's current rule); allocates
//   nothing after warm-up, but the containers must live somewhere across frames.
// - LinearArena: new std::pmr containers every frame on an arena reset per frame; allocates nothing after
//   warm-up and needs no state besides the arena.
// - monotonic_buffer_resource: the same with the standard resource, release()d per frame, which returns its
//   blocks upstream, so every frame allocates them again.
//
// The two cases, each with 5 000 ids (the showcase game's enemy count, #256):
// - grid: per-cell id lists for a 32 x 32 broadphase grid (1 024 lists of about 5 ids). O(ids + cells).
// - collect + sort: the ids that pass a filter (half) in one list, sorted. O(n log n).

#include <teserra/core/linear_arena.hpp>
#include <teserra/core/types.hpp>

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory_resource>
#include <vector>

using namespace teserra;

namespace {

constexpr u32 kIds = 5'000;
constexpr usize kCells = usize{32} * 32;

// The cell of an id: a fixed scatter, so every variant builds the same lists.
constexpr usize cellOf(u32 id) {
    return (static_cast<usize>(id) * 2'654'435'761U) % kCells;
}

// A key that puts the ids out of order, so the sort has work to do.
constexpr u32 keyOf(u32 id) {
    return (id * 40'503U) ^ (id >> 3U);
}

template <typename Cells>
u64 fillAndSumCells(Cells& cells) {
    for (u32 id = 0; id < kIds; ++id) {
        cells[cellOf(id)].push_back(id);
    }
    u64 sum = 0;
    for (usize cell = 0; cell < kCells; ++cell) {
        for (const u32 id : cells[cell]) {
            sum += id * cell; // reads every list in order, like a pair test would
        }
    }
    return sum;
}

template <typename List>
u64 collectAndSort(List& list) {
    for (u32 id = 0; id < kIds; ++id) {
        if (keyOf(id) % 2 == 0) {
            list.push_back(keyOf(id));
        }
    }
    std::ranges::sort(list);
    return list.front() + list.back() + list.size();
}

} // namespace

TEST_CASE("LinearArena: grid of per-cell lists, 5 000 ids in 1 024 cells", "[!benchmark][core][arena]") {
    BENCHMARK("fresh std::vector") {
        std::vector<std::vector<u32>> cells(kCells);
        return fillAndSumCells(cells);
    };

    std::vector<std::vector<u32>> reused(kCells);
    BENCHMARK("reused std::vector (clear)") {
        for (std::vector<u32>& cell : reused) {
            cell.clear();
        }
        return fillAndSumCells(reused);
    };

    LinearArena arena;
    BENCHMARK("std::pmr::vector on LinearArena, reset per frame") {
        arena.reset();
        std::pmr::vector<std::pmr::vector<u32>> cells(kCells, &arena);
        return fillAndSumCells(cells);
    };

    std::pmr::monotonic_buffer_resource monotonic;
    BENCHMARK("std::pmr::vector on monotonic_buffer_resource, release per frame") {
        monotonic.release();
        std::pmr::vector<std::pmr::vector<u32>> cells(kCells, &monotonic);
        return fillAndSumCells(cells);
    };
}

TEST_CASE("LinearArena: collect and sort 2 500 of 5 000 ids", "[!benchmark][core][arena]") {
    BENCHMARK("fresh std::vector") {
        std::vector<u32> list;
        return collectAndSort(list);
    };

    std::vector<u32> reused;
    BENCHMARK("reused std::vector (clear)") {
        reused.clear();
        return collectAndSort(reused);
    };

    LinearArena arena;
    BENCHMARK("std::pmr::vector on LinearArena, reset per frame") {
        arena.reset();
        std::pmr::vector<u32> list(&arena);
        return collectAndSort(list);
    };

    std::pmr::monotonic_buffer_resource monotonic;
    BENCHMARK("std::pmr::vector on monotonic_buffer_resource, release per frame") {
        monotonic.release();
        std::pmr::vector<u32> list(&monotonic);
        return collectAndSort(list);
    };
}
