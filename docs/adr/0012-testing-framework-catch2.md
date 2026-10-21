# ADR-0012: Catch2 v3 as the test and micro-benchmark framework

- **Status:** Accepted
- **Date:** 2026-10-21
- **Related:** [testing strategy](../guides/testing-strategy.md), [memory and performance](../architecture/memory-and-performance.md#benchmarking-policy)

## Context

We need a unit test framework that integrates with CTest and IDE test explorers, produces CI-friendly reports, and is actively maintained for the lifetime of an open-source engine. We also need micro-benchmarks for the ECS, allocators and sprite batching, and would prefer not to add a second framework for that.

The shortlist was doctest and Catch2; GoogleTest is listed for completeness.

## Decision

- **Catch2 v3** (via vcpkg, feature `tests`) for all unit, integration and GPU tests.
- Tests are registered per test case with `catch_discover_tests()`, so CTest, Visual Studio and CLion show individual tests.
- CI uses the JUnit reporter for test result annotations.
- **Micro-benchmarks use Catch2's `BENCHMARK`** in separate benchmark executables (`benchmarks/`), built only with `TESERRA_BUILD_BENCHMARKS=ON`. Benchmark results are never part of the pass/fail test suite.
- Engine code is compiled with exceptions enabled at the toolchain level ([ADR-0016](0016-error-handling-policy.md)); Catch2's `REQUIRE_THROWS` is not used for engine code because engine code does not throw.

## Alternatives considered

| Option | Why not |
|---|---|
| doctest | Fastest compile times and a single header, and very pleasant to use. Its release cadence has slowed considerably, and it has no built-in benchmarking; we would add nanobench as a second dependency. Close second; trivially portable to later because the macro style is nearly identical. |
| GoogleTest + Google Benchmark | Industry standard and rich, but two dependencies, heavier fixtures model, and slower compile than both alternatives. |
| Custom test runner | Not a learning goal. |

## Consequences

**Positive**
- One dependency for tests and micro-benchmarks, active maintenance, rich matchers and generators, good reporters.

**Negative / costs**
- Catch2 v3 is a compiled library: slightly slower initial build than doctest. Test TUs stay small to keep incremental builds fast.

## Revisit if

- Test build time becomes a measurable drag on iteration speed (incremental test build > ~10 s for a one-file change): evaluate doctest.
- Benchmarks need features Catch2 lacks (hardware counters): add nanobench for those benchmarks only.
