#pragma once

// Test helper: counts heap allocations, to check that a steady-state frame allocates nothing
// (docs/architecture/memory-and-performance.md#verifying-the-allocation-rule). Every executable from
// teserra_add_test() replaces the global operator new and delete with versions that count the allocations
// of each thread and otherwise behave like the standard ones.
//
//   recordFrame(list); // warm-up
//   list.reset();
//   const teserra::test::AllocationCounter allocations;
//   recordFrame(list);
//   CHECK(allocations.count() == 0);

#include <teserra/core/types.hpp>

namespace teserra::test {

/// Heap allocations made by the current thread since it started: every operator new and new[], aligned or
/// not.
[[nodiscard]] u64 threadAllocationCount() noexcept;

/// Counts the heap allocations the current thread makes while the counter is alive.
class AllocationCounter {
public:
    AllocationCounter() noexcept
        : m_start(threadAllocationCount()) {}

    /// Allocations since construction.
    [[nodiscard]] u64 count() const noexcept { return threadAllocationCount() - m_start; }

private:
    u64 m_start;
};

} // namespace teserra::test
