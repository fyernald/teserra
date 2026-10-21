# ADR-0016: Error handling: results, assertions, no exceptions in engine code

- **Status:** Accepted
- **Date:** 2026-10-21
- **Related:** [coding standards](../guides/coding-standards.md#error-handling), [ADR-0011](0011-logging-spdlog-facade.md), [ADR-0002](0002-cpp20-language-standard.md)

## Context

Game engines face three kinds of failure:

1. **Programmer errors** (invalid handle, out-of-range index, calling a method in the wrong state). These are bugs.
2. **Recoverable runtime errors** (file not found, shader failed to compile, unsupported format, device creation failed). Callers can and should react.
3. **Unrecoverable conditions** (out of memory in a critical path, GPU device lost in v1, corrupted internal state).

Exceptions make control flow invisible, complicate frame-time reasoning and do not cross some boundaries we may need later (scripting VMs, C APIs). Error codes alone are easy to ignore.

## Decision

1. **Programmer errors → assertions.** `<teserra/core/assert.hpp>` provides:
   - `TESERRA_ASSERT(cond, fmt, ...)`: checked in Debug and `RelWithDebInfo`, compiled out in Release.
   - `TESERRA_VERIFY(cond, fmt, ...)`: condition always evaluated; reports in Debug/RelWithDebInfo.
   - `TESERRA_UNREACHABLE()`, `TESERRA_FATAL(fmt, ...)` (always active: logs, flushes, breaks into debugger if attached, aborts).
   - A replaceable assert handler (`setAssertHandler`) so tests can turn failed assertions into test failures.
2. **Recoverable errors → `teserra::Result<T>`.** `Result<T>` is `tl::expected<T, Error>` today and `std::expected<T, Error>` when C++23 is adopted. `Error` holds an `ErrorCode` enum, a module tag and a short message. Fallible functions return `Result` and are `[[nodiscard]]`. `Result<void>` for operations without a value.
3. **Unrecoverable → `TESERRA_FATAL`**. No attempt to unwind.
4. **Exceptions:** the toolchain keeps exceptions **enabled** (third-party libraries and the standard library may throw, e.g. `std::bad_alloc`), but **engine code never throws** and never uses `try/catch` for control flow. Public engine functions that cannot fail are `noexcept`. Exceptions escaping a third-party call are a bug and are caught at the narrowest boundary, converted to `Error`, and logged.
5. Every error is **logged once, where it is handled**, not where it is created.

## Alternatives considered

| Option | Why not |
|---|---|
| Exceptions throughout | Hidden control flow, harder frame-time reasoning, awkward for future scripting/C boundaries. |
| Exceptions disabled at compile time (`-fno-exceptions`, `/EHs-c-`) | Cleanest in theory; requires configuring every dependency (spdlog, fmt, Catch2, standard library behavior on allocation failure). Deferred: may become an optional build mode. |
| Plain error codes + out params | Easy to ignore, verbose. `Result` + `[[nodiscard]]` gives the same cost with better ergonomics. |
| Custom `Result` type | `tl::expected` mirrors the future standard API exactly; writing our own adds no learning value. |

## Consequences

**Positive**
- Failure modes are explicit in signatures; tests can check error paths without exception machinery.
- Smooth migration path to `std::expected`.

**Negative / costs**
- `tl-expected` is a (tiny, header-only) public dependency of `teserra_core`.
- Some verbosity at call sites; mitigated by a `TESERRA_TRY(expr)` helper macro that propagates errors.

## Revisit if

- We want a no-exceptions build for a constrained platform: add a CMake option and fix dependency configuration.
- All target compilers support `<expected>`: switch the alias.
