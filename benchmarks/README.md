# Benchmarks

Catch2 micro-benchmarks ([benchmarking policy](../docs/architecture/memory-and-performance.md#benchmarking-policy)). They build only with `TESERRA_BUILD_BENCHMARKS=ON`, in Release:

```sh
cmake --preset windows-msvc-bench
cmake --build --preset windows-msvc-bench
build/windows-msvc-bench/benchmarks/teserra_ecs_bench "[ecs]" --benchmark-samples 50
build/windows-msvc-bench/benchmarks/teserra_core_bench "[arena]" --benchmark-samples 50
```

Benchmarks are tagged `[!benchmark]`, so a plain run lists nothing; select them by tag (`[ecs]`, `[arena]`) or by name. Compare numbers only from the same machine. The `Benchmarks` workflow runs them on CI for reference, but CI runners are too noisy for thresholds.

## Reference machine

The numbers in the docs and in optimization PRs come from this machine unless a PR says otherwise.

| | |
|---|---|
| CPU | AMD Ryzen 7 5700X, 8 cores / 16 threads, 3.4 GHz base |
| RAM | 32 GB |
| GPU | NVIDIA GeForce RTX 3080, driver 617.42 (32.0.16.1742) |
| OS | Windows 10 Pro 22H2 (10.0.19045) |
| Compiler | MSVC 14.51 (Visual Studio 18), Release (`windows-msvc-bench`) |

## Core (`core/linear_arena_bench.cpp`)

`LinearArena` (#136), 50 samples, mean on the reference machine. Each benchmark builds one frame's transient containers from 5,000 ids: fresh `std::vector`s, reused `std::vector`s (cleared), `std::pmr` containers on a `LinearArena` reset per frame, or on a `std::pmr::monotonic_buffer_resource` released per frame.

| Benchmark | What it measures | Fresh | Reused | Arena | Monotonic |
|---|---|---|---|---|---|
| grid of per-cell lists | 1,024 lists of about 5 ids filled and read, O(ids + cells) | 252 µs | 10.2 µs | 57.5 µs | 56.2 µs |
| collect and sort | 2,500 of 5,000 ids collected into one list and sorted, O(n log n) | 20.7 µs | 18.6 µs | 21.9 µs | 23.2 µs |

## ECS (`ecs/ecs_bench.cpp`)

Baseline from #114, 50 samples, mean on the reference machine:

| Benchmark | What it measures | Mean |
|---|---|---|
| create 100k | `World::create()` x 100 000 | 1.42 ms |
| destroy 100k | `World::destroy()` x 100 000; half have three components | 1.92 ms |
| iterate Position | a one-component view over 100 000 | 538 µs |
| iterate Position, Velocity, Health | a three-component view over 100 000, writing one | 1.10 ms |
| add + remove Selected x 10k | a tag added to 10 000 of 100 000 entities, then removed | 162 µs |
| iterate Position with Velocity (50%) | a two-component view, the second on half of 100 000 | 412 µs |
