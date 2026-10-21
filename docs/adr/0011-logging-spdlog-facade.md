# ADR-0011: Logging through an engine facade over spdlog

- **Status:** Accepted
- **Date:** 2026-10-21
- **Related:** [ADR-0016](0016-error-handling-policy.md), [coding standards](../guides/coding-standards.md#logging)

## Context

Every subsystem logs. Logging must be fast when disabled, structured by category, compile-time checked, and must not drag a third-party library into every header. spdlog is fast and full-featured; we want its sinks and async machinery but not its API surface everywhere.

## Decision

- The engine logs through macros in `<teserra/core/log.hpp>`:

  ```cpp
  TESERRA_LOG_INFO(LogCategory::Renderer, "Created swapchain {}x{}", width, height);
  TESERRA_LOG_WARN(...); TESERRA_LOG_ERROR(...); TESERRA_LOG_DEBUG(...); TESERRA_LOG_TRACE(...);
  ```

- Formatting uses **`std::format`** with compile-time checked format strings (`std::format_string`). The facade formats into a stack buffer and passes a `std::string_view` plus `std::source_location` to a non-template `log::detail::emit(...)` implemented in a `.cpp` file.
- **spdlog is a `PRIVATE` dependency** of `teserra_core`; no spdlog header appears in any engine header.
- Levels: `Trace, Debug, Info, Warn, Error, Critical`. A compile-time floor `TESERRA_LOG_LEVEL_MIN` removes lower-level calls entirely (default: `Trace` in Debug, `Info` in Release). A runtime per-category level filters the rest.
- Categories are an engine enum (`Core, Platform, Rhi, Renderer, Assets, Ecs, Scene, App, Game`), plus user-defined string categories for games.
- Sinks: colored console and rotating file by default; a callback sink lets tests and the future ImGui console capture output.
- Logging is **not** an error-handling mechanism ([ADR-0016](0016-error-handling-policy.md)): errors are returned or asserted, and *also* logged where they are handled.

## Alternatives considered

| Option | Why not |
|---|---|
| spdlog directly | Couples every TU to spdlog/fmt headers; slower compiles; replacement becomes a global edit. |
| fmt-based facade (expose `fmt::format_string`) | Makes fmt a public dependency; `std::format` covers our needs with C++20 compilers ([ADR-0002](0002-cpp20-language-standard.md)). |
| Custom logger from scratch | Sinks, rotation and async queues are solved problems. The facade is the part worth owning. |
| Quill / glog | Quill is faster but less common; glog is heavier and less game-oriented. |

## Consequences

**Positive**
- Cheap disabled logs (compiled out), category filtering, compile-time format checking.
- spdlog can be replaced by editing one `.cpp` file.

**Negative / costs**
- spdlog internally uses fmt, so a Release binary contains both `std::format` and fmt code. Acceptable size cost.

## Revisit if

- Log formatting shows up in profiles (consider deferred formatting / binary logging).
