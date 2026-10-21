# ADR-0015: Windows first, behind a platform abstraction layer

- **Status:** Accepted
- **Date:** 2026-10-21
- **Related:** [platform abstraction](../architecture/platform-abstraction.md), [ADR-0006](0006-glfw-behind-engine-interfaces.md), [build and CI](../guides/build-and-ci.md)

## Context

The author develops on Windows, and most indie players are on Windows. Linux must be an easy addition later, which means platform assumptions cannot leak into engine code.

## Decision

- Windows 10/11 x64 is the only **supported** platform in v0.1.
- All OS-specific functionality lives behind interfaces in `teserra_platform`: windowing (GLFW backend), input, filesystem paths and file IO, timing, dynamic library loading, thread naming/affinity helpers. Portable C++ facilities (`std::thread`, `std::mutex`, `std::atomic`, `std::chrono::steady_clock`, `std::filesystem`) are used directly and are not re-wrapped without a reason.
- Platform-specific sources live only in `engine/platform/src/<platform>/` (`windows/`, `posix/` later) and are selected by CMake (`if(WIN32)` / `if(UNIX)`), not by `#ifdef` in shared code.
- Platform detection macros (`TESERRA_PLATFORM_WINDOWS`, `TESERRA_PLATFORM_LINUX`) come from `<teserra/core/config.hpp>`. Using them outside `engine/platform/` and `engine/core/config*` requires a review comment explaining why.
- A **Linux CI job** that builds and tests the platform-independent modules (core, ecs, rhi null backend) exists from iteration 9 (when the ECS and RHI null backend give it meaningful portable code to build) as a non-blocking job, so Windows-only assumptions are caught before the port.
- Strings at the platform boundary are UTF-8 (`std::string`, `std::filesystem::path` internally); Windows wide-char conversion happens only in `src/windows/`.

## Alternatives considered

| Option | Why not |
|---|---|
| Windows-only now, port later | Ports are always more expensive than expected when assumptions have spread. |
| Linux and Windows both supported from v0.1 | Doubles testing effort at 5-10 h/week. The CI job gives most of the safety at a fraction of the cost. |

## Consequences

**Positive**
- The Linux port becomes "implement `src/posix/`, enable the GLFW backend on Linux, add CI job".

**Negative / costs**
- Some interface work up front (filesystem paths, executable directory) that a Windows-only engine would skip.

## Revisit if

- A contributor offers to maintain Linux as a supported platform earlier (welcome; see the port checklist in [platform-abstraction.md](../architecture/platform-abstraction.md#new-platform-port-checklist)).
