# ADR-0023: Profiling through Tracy behind engine macros

- **Status:** Accepted
- **Date:** 2026-10-21
- **Related:** [memory and performance](../architecture/memory-and-performance.md#profiling)

## Context

"Profile early" only works if instrumentation exists before performance problems do. Instrumentation must cost nothing when disabled and must not tie engine code to a specific profiler.

## Decision

- Engine code instruments through macros in `<teserra/core/profile.hpp>`: `TESERRA_PROFILE_SCOPE(name)`, `TESERRA_PROFILE_FUNCTION()`, `TESERRA_PROFILE_FRAME_MARK()`, `TESERRA_PROFILE_ALLOC(ptr, size)` / `TESERRA_PROFILE_FREE(ptr)`, `TESERRA_PROFILE_THREAD_NAME(name)`.
- The macros exist from iteration 2 as **no-ops**. **Tracy** (via vcpkg, feature `profiling`) is wired in later behind `TESERRA_ENABLE_PROFILING` (default `OFF`, `ON` in a `profile` preset).
- GPU zones are implemented inside the RHI backends only (Tracy's GL zones in the GL backend); the renderer annotates passes through an RHI debug-marker API (`cmd.pushDebugGroup(name)`), which also feeds RenderDoc.

## Alternatives considered

| Option | Why not |
|---|---|
| Optick | Good, less actively maintained. |
| Superluminal / VTune / PIX only | Excellent sampling profilers; complementary, not a replacement for instrumented frame timelines; not free/open for all contributors. |
| Custom profiler | Not a v1 learning priority. A simple built-in frame-time overlay comes with the ImGui debug tools (v0.2). |

## Consequences

**Positive**
- Zero cost when disabled; a world-class timeline profiler when enabled.

**Negative / costs**
- Contributors need the Tracy viewer matching the client version; documented in build-and-ci.

## Revisit if

- Tracy's license or maintenance changes, or a different profiler offers clearly better GPU integration across GL and Vulkan.
