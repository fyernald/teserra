# Memory and performance

Performance is a design input, not a late optimization pass. But "fast" claims require numbers: Teserra optimizes with benchmarks and profiles, never by intuition alone.

Related: [ADR-0023 profiling](../adr/0023-profiling-tracy.md), [ADR-0019 ECS storage](../adr/0019-ecs-storage-sparse-sets.md), [testing strategy](../guides/testing-strategy.md).

## Principles

1. **No per-frame heap allocations in hot paths after warm-up.** Hot paths: input processing, schedule execution infrastructure, ECS iteration, render extraction, renderer prepare/record, RHI command recording and submission. Gameplay systems written by users are not bound by this rule, but engine-provided systems are.
2. **Data-oriented by default.** Contiguous arrays of plain data, processed in batches. Pointer-chasing object graphs are the exception and need a reason.
3. **Ownership is explicit.** Every allocation has one owner (RAII). Long-lived objects live in pools or containers owned by a service; transient per-frame data lives in reused buffers or a frame arena.
4. **Measure before and after.** Every optimization PR includes benchmark or profile numbers.

## Allocation strategy

| Lifetime | Strategy (v1) | Future |
|---|---|---|
| Engine lifetime (services, pools) | Normal allocation at startup, owned by `App` or the service | - |
| Resource lifetime (textures, buffers, assets) | `HandlePool<T>` slot arrays with FIFO free lists | Per-type block allocators |
| Per-frame transient (command streams, sort arrays, batch lists, extracted render data) | **Reused containers**: `clear()` keeps capacity; capacity grows during warm-up and then stays stable | - |
| Per-frame transient without a place to live between frames (lists built inside a system, nested lists whose number changes) | **`LinearArena`** frame arena with `std::pmr` containers, reset once per frame (see [frame arena](#frame-arena)) | Per-thread arenas when systems run in parallel |
| GPU per-frame data (vertices, uniforms) | Upload ring buffer partitioned by frame slot | Same, plus staging for Vulkan |
| Small fixed-size objects with churn | Object pool (`Pool<T>`) | - |

Rules:

- No `new`/`delete` in engine code; use `std::unique_ptr`, containers or pools.
- No `std::shared_ptr` for engine objects unless shared ownership is genuinely required and documented (asset handles use their own reference counting, see [assets](assets.md#handles-and-ids)).
- Prefer `std::vector` + `reserve` over node-based containers (`std::map`, `std::list`) in engine code. Hash maps on hot paths need a profile showing they matter; then use an open-addressing map.
- Avoid `std::function` on hot paths (allocation + indirect call); prefer templates or function pointers + context.
- Strings: no per-frame `std::string` construction; use `std::string_view`, interned `StringId`s (hashed names) for lookups.

### Frame arena

`LinearArena` (`<teserra/core/linear_arena.hpp>`, #136) is a `std::pmr::memory_resource` that hands out memory by bumping a pointer. Deallocation does nothing; `reset()` takes everything back at once and keeps the memory. If a frame needed more than one block, `reset()` merges them into one block of the total size, so once the largest frame has run, frames make no upstream allocations. `std::pmr::monotonic_buffer_resource` is different: its `release()` returns the blocks, so every frame allocates them again.

```cpp
arena.reset(); // once per frame, before anything is built on it
std::pmr::vector<std::pmr::vector<u32>> cells(kCells, &arena);
```

- **When to use it:** for transient containers that have nowhere to live between frames, such as lists built inside a gameplay system or nested lists whose count changes from frame to frame. Containers kept in their owner and cleared every frame remain the rule for engine code, because they are faster still (benchmark below).
- **Lifetime:** nothing built on the arena may outlive the next `reset()`. Data that the GPU or a later frame still reads needs one arena per frame slot (`kMaxFramesInFlight`), each reset when its slot comes round again. The arena is not thread-safe, so parallel systems will need one arena per thread.
- **Debug checks:** when `TESERRA_ENABLE_ASSERTS` is on, `reset()` fills the memory it takes back with `LinearArena::kPoison` (`0xDD`), and deallocating a pointer that was not handed out since the last reset asserts. That catches a container that outlived the reset when it grows or is destroyed. With AddressSanitizer the memory is also poisoned for ASan, so any access through a stale pointer stops the program (`sanitizers.arena_use_after_reset`).

The benchmark is `benchmarks/core/linear_arena_bench.cpp`: 5,000 ids, Release, on the reference machine, 50 samples, mean.

| Frame work | Fresh `std::vector` | Reused `std::vector` | `LinearArena` | `monotonic_buffer_resource` |
|---|---|---|---|---|
| Per-cell lists of a 32×32 grid | 252 µs | 10 µs | 58 µs | 56 µs |
| Collect 2,500 ids and sort them | 20.7 µs | 18.6 µs | 21.9 µs | 23.2 µs |

The arena is 4.4× faster than building fresh containers when a frame makes many small ones, and it allocates nothing after warm-up. Reused containers are 5.7× faster again, because their lists already have their capacity and never grow. For one list the sort dominates, and every variant costs about the same.

### Verifying the allocation rule

Every test executable replaces the global `operator new` / `delete` with versions that count allocations per thread (`tests/support/`). `teserra::test::AllocationCounter` from `<teserra/test/allocation_counter.hpp>` counts the current thread's allocations while it is alive. Tests for the renderer, ECS views and command recording assert **zero allocations** in a steady-state frame after a warm-up frame. This turns principle 1 into a regression test.

**The whole frame** (#112, `engine/engine/tests/app_allocation_test.cpp`): an App on the null backend runs a busy scene for 12 warm-up frames, then counts 30 more and expects zero allocations. The scene has a two-layer tilemap over four chunks, 150 sprites with colliders that a moving player sweeps through (collision events every frame), a camera that is the player's child, debug lines and the collider debug view, and a system that destroys and respawns an entity every frame. A second test adds a system that allocates once per frame and expects the counter to see every one, so the first test cannot pass by counting nothing.

```cpp
recordFrame(list); // warm-up
list.reset();
const test::AllocationCounter allocations;
recordFrame(list);
CHECK(allocations.count() == 0);
```

## Profiling

- Instrumentation macros (`TESERRA_PROFILE_SCOPE`, `TESERRA_PROFILE_SCOPE_DYNAMIC`, `TESERRA_PROFILE_FUNCTION`, `TESERRA_PROFILE_FRAME_MARK`) exist from the core iteration and are placed in every engine subsystem's main entry points as they are written. Without profiling they expand to nothing.
- **Tracy 0.13.1** is enabled with the `windows-msvc-profile` preset (`TESERRA_ENABLE_PROFILING=ON`, `RelWithDebInfo`; vcpkg feature `profiling`, Tracy's `on-demand` mode, so a run records only while a viewer is connected). Use the matching viewer version. Builds without it link no Tracy at all; a CTest case checks that an `OFF` build carries no Tracy strings and a profile build does.
- **What the timeline shows (#107):** frames (`App` frame mark); `Events`, `Render`, `Present` and `Assets end frame` in the App; every scene phase (`PreUpdate`, `Update`, ...) and every system by its name (`move_player`, `teserra.transforms`, ...) with the `Commands` sync point; `Renderer::render` and every pass (`Sprites`, `Debug`, `Composite`); the GL device's `beginFrame` (the frame-slot fence wait), `submit` and `endFrame`, and the swapchain's `present`. **GPU zones:** each debug group the GL backend replays (each pass) is timed with timer queries on an `OpenGL` GPU timeline (`gl_gpu_profiler.cpp`, the only file that includes Tracy's GL header; it routes Tracy's GL calls to the device's glad context).
- **Reading a capture:** with vsync the CPU waits for the display somewhere in the frame. On NVIDIA drivers it is the first GL call after the buffer swap, so the wait shows up in `GlDevice::submit`, not in `present`. Subtract it before reading CPU cost.
- **Without the viewer:** `tracy-capture -o run.tracy -s 8` records a running profile build, and `tracy-csvexport` (`-e` for self times, `-g` for GPU zones) turns it into CSV. Both come with vcpkg's `tracy[cli-tools]`.
- Use sampling profilers (Visual Studio profiler, Superluminal, VTune) for hotspot hunting; Tracy for frame timelines and spikes.
- RenderDoc for GPU frame captures; RHI debug names make captures readable.

## Benchmarking policy

- Micro-benchmarks use Catch2 `BENCHMARK` in `benchmarks/` ([ADR-0012](../adr/0012-testing-framework-catch2.md)), built with `TESERRA_BUILD_BENCHMARKS=ON` in Release (`RelWithDebInfo` for profiling).
- Every benchmark documents what it measures, the data size, and the expected complexity.
- **CI runs benchmarks in a non-blocking job** (`bench.yml`, on pushes to main that touch core, ecs or benchmarks, and by hand) and uploads results as artifacts. CI runners are too noisy for thresholds; regressions are judged on the reference machine.
- An optimization PR must include: the benchmark (new or existing), before/after numbers from the same machine, and the machine description.
- A benchmark that has no consumer for two minor versions is deleted.

## v1 performance budget

Reference machine: the author's development PC, documented in [`benchmarks/README.md`](../../benchmarks/README.md) (AMD Ryzen 7 5700X, 32 GB, RTX 3080, Windows 10). Targets for v0.1 on that machine, at 1920×1080:

| Scenario | Target |
|---|---|
| 10 000 moving sprites, 16 textures, one camera | ≥ 60 FPS, CPU frame time ≤ 8 ms |
| Tilemap 256×256 tiles, 3 layers, scrolling camera | ≥ 60 FPS, chunk rebuild only on change |
| ECS: iterate 100 000 entities with 2 components | ≤ 1 ms |
| Steady-state engine frame | 0 heap allocations in engine code |
| Startup to first frame (demo game) | ≤ 2 s |

If a target is missed, the gap is recorded in the iteration retro and becomes a backlog item; targets are not silently lowered.

## Data-oriented design checklist (for reviews)

- Is the hot data contiguous, and is cold data separated from hot data?
- Is work done in batches rather than per object through virtual calls?
- Are allocations bounded and front-loaded?
- Is there a benchmark or profile supporting any non-obvious optimization?
- Does the design still allow moving the work to another thread later (no hidden shared mutable state)?
