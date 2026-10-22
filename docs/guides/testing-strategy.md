# Testing strategy

Tests are how a solo developer keeps moving fast without breaking things. Every layer has a test style that matches it. Framework: Catch2 v3 ([ADR-0012](../adr/0012-testing-framework-catch2.md)).

## Test pyramid

```mermaid
flowchart TB
    E["Example smoke runs<br/>(run each example N frames)"]
    S["Snapshot tests<br/>(render → readback → compare to golden image)"]
    G["GPU integration tests<br/>(real backend, offscreen)"]
    R["RHI/renderer tests on the null backend<br/>(recorded command inspection)"]
    U["Unit tests<br/>(core, platform logic, ecs, assets, scene)"]
    E --- S --- G --- R --- U
```

Most tests are at the bottom: fast, deterministic, no GPU, run on every build. The upper levels catch integration and visual regressions.

## Unit tests

- Location: `engine/<module>/tests/`, one executable per module (`teserra_<module>_tests`), registered with `catch_discover_tests`.
- Naming: `TEST_CASE("HandlePool: stale handle is rejected", "[core][handle]")`.
- Must run in milliseconds, without files outside the test's own data, without a window or GPU, and without sleeping. Time is injected via a manual `Clock`.
- Assertions: every test executable installs a throwing handler with `setAssertHandler`, so a triggered `TESERRA_ASSERT` fails the current test with its message instead of aborting the run. Expected failures are tested with `REQUIRE_ASSERTS(expr)`, or `teserra::test::requireAssert([&] { ... })` to inspect the failure (kind, expression, message, location); see `<teserra/test/assert.hpp>`. Tests that need a failing assertion are wrapped in `#if TESERRA_ENABLE_ASSERTS`.
- **Randomized model tests** for data structures: the ECS and `HandlePool` are tested by applying random operation sequences to both the real implementation and a naive reference (e.g. `std::map`) and comparing state after every step. Seeds are printed on failure.

### Adding a test executable

`cmake/TeserraTesting.cmake` provides one helper, used from `engine/<module>/tests/CMakeLists.txt`:

```cmake
teserra_add_test(core
    SOURCES config_test.cpp version_test.cpp
    DEPENDS core
)
```

It creates `teserra_core_tests` linked to `Catch2::Catch2WithMain` and to `teserra_test_support` (`tests/support/`: the `<teserra/test/...>` helpers and a listener that turns off console logging and installs the throwing assert handler), applies the warning set, and registers every `TEST_CASE` as its own CTest test labeled with the module name (discovery runs when CTest starts, not at build time). Useful commands:

```bash
ctest --preset windows-msvc-debug -N          # list test cases
ctest --preset windows-msvc-debug -L core     # one module
ctest --preset windows-msvc-debug -R "version:"  # by name
```

More helpers cover guarantees that are about compilation or about ending the process:

```cmake
# Builds every public header of the module in a translation unit of its own (target
# teserra_core_header_check, part of the normal build). Fails if a header is not self-contained or
# exposes spdlog or fmt (ADR-0011).
teserra_add_header_check(core)

# CTest test that builds the file (excluded from the normal build) and passes only if the compiler
# output matches the regex, e.g. a mismatched log format string. Labeled compile_fail and <module>.
teserra_add_compile_fail_test(core.log.format_string_is_checked
    SOURCE compile_fail/log_format_mismatch.cpp
    EXPECT "basic_format_string"
    DEPENDS core
)

# WARNINGS_AS_ERRORS makes a warning (e.g. a discarded [[nodiscard]] Result) fail the build every time.
teserra_add_compile_fail_test(core.result.discarding_is_diagnosed
    SOURCE compile_fail/result_discarded.cpp
    EXPECT "nodiscard"
    WARNINGS_AS_ERRORS
    DEPENDS core
)

# CTest test that runs a small program (no Catch2) and passes only if it ends abnormally with output
# matching the regex, e.g. TESERRA_FATAL with the default handler. Labeled abort and <module>.
teserra_add_abort_test(core.assert.fatal_aborts
    SOURCE fatal_abort.cpp
    EXPECT "Fatal error [(][^)]*fatal_abort[.]cpp:[0-9]+[)]: GPU device lost: driver reset"
    DEPENDS core
)
```

Logging is observed through `teserra::log::CallbackSink` (`teserra::test::LogCapture` in `<teserra/test/log_capture.hpp>`), never by parsing console output.

The `ci-windows` test preset also writes a JUnit report to `build/ci-windows/junit.xml`, which CI turns into annotations. Build-system tests (CTest label `cmake`) live in `tests/cmake/` and configure small fixture projects, for example to check the layer rule.

## RHI and renderer tests

- **Null backend** (`teserra_rhi_null`): renderer tests build a `FramePacket` by hand, render one frame, and assert on the recorded stream ("2 batches, 1 pipeline switch, bind group 2 changes once"). RHI validation tests assert that invalid usage is detected (draw outside a render pass, mismatched pipeline/target formats, stale handles).
- **Command stream tests**: record → serialize → compare to expected sequence.
- **Allocation tests**: a steady-state frame performs zero heap allocations ([memory and performance](../architecture/memory-and-performance.md#verifying-the-allocation-rule)).

## GPU integration tests

- Location: `tests/gpu/`. Use the real OpenGL backend with a hidden window (or the headless mechanism chosen by the spike), render offscreen, read back.
- Locally they run with the normal test command if a GPU is available; they skip (not fail) with a clear message when no suitable context can be created, unless `TESERRA_REQUIRE_GPU_TESTS=1` is set (CI sets it on GPU jobs).
- **Headless GPU testing in CI** is decided by a time-boxed spike (candidates: Mesa llvmpipe for Windows, i.e. a software OpenGL 4.5 implementation shipped as `opengl32.dll` next to the test executable; or a Linux runner with Mesa llvmpipe once the GL backend builds on Linux). Until then, GPU tests run locally before merging rendering PRs (PR checklist item).
- Key GPU tests: orientation and winding tests ([ADR-0024](../adr/0024-coordinate-conventions.md)), clear/triangle/textured quad, bind group remapping, frames-in-flight ring buffer correctness, swapchain resize.

## Snapshot tests

- Render a scene to an offscreen target at a fixed resolution, read back RGBA8, compare with `tests/golden/<test>.png`.
- Comparison: per-channel tolerance (default 2/255) and maximum differing pixel ratio (default 0.1%), configurable per test because software and hardware rasterizers differ slightly. Prefer scenes that avoid driver-dependent filtering (nearest sampling, axis-aligned quads).
- On failure, the actual image and a diff image are written to `tests/golden/_actual/` and `_diff/` (git-ignored) and uploaded as CI artifacts.
- Updating goldens: run the test binary with `--update-goldens`, review the image change in the PR (GitHub renders PNG diffs). A golden update without an explanation in the PR description is rejected.

## Example smoke runs

Every example accepts `--frames N --hidden-window`; CI (GPU job) runs each for 120 frames and fails on non-zero exit, assertion or `Error`-level log. This keeps examples from rotting.

## Benchmarks

Separate from tests, see [benchmarking policy](../architecture/memory-and-performance.md#benchmarking-policy).

## What every PR needs

| Change | Required tests |
|---|---|
| Bug fix | Regression test that fails before the fix |
| New core/ecs/assets/scene logic | Unit tests for normal and error paths |
| New RHI feature | Validation tests (null backend) + a GPU test or example exercising it |
| Renderer feature | Null-backend command test; snapshot test once the snapshot harness exists |
| Public API change | Example or test using the new API; dream code in [public-api.md](../architecture/public-api.md) updated if affected |
| Performance change | Benchmark before/after (not a test) |
