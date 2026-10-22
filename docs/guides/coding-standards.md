# Coding standards

These rules apply to all engine, tool, example and test code. Formatting and many naming rules are enforced by `clang-format` and `clang-tidy`; the rest is checked in review. Naming is specified in [ADR-0021](../adr/0021-naming-conventions.md); patterns in the [pattern catalog](patterns-and-conventions.md).

## Language level

- ISO C++20, no compiler extensions, no modules ([ADR-0002](../adr/0002-cpp20-language-standard.md)).
- Warnings are errors in CI: MSVC `/W4 /permissive- /WX`; Clang/GCC `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror`. Third-party headers are included as `SYSTEM` so their warnings do not count.
- No `#pragma warning(disable)` / `-Wno-*` in engine code without a comment explaining the false positive.

## Files and headers

- `#pragma once` in every header. Headers are self-contained (compile on their own; checked by the `teserra_<module>_header_check` target from `teserra_add_header_check()`).
- Include order: the file's own header, then `<teserra/...>` headers, then third-party, then standard library. clang-format groups them.
- Full-path includes only: `#include <teserra/core/handle.hpp>`.
- Forward-declare instead of including where a pointer/reference suffices. Public headers include the minimum.
- No `using namespace` at namespace scope in headers. In `.cpp` files, `using namespace teserra;` is acceptable inside functions or after includes for the file's own module namespace.

## Types and ownership

- Prefer value types and aggregates. Use `explicit` on single-argument constructors and conversion operators.
- Ownership in signatures follows the [pattern catalog conventions](patterns-and-conventions.md#conventions-that-are-not-patterns-but-are-enforced-the-same-way).
- No raw `new`/`delete`. No `std::shared_ptr` without a documented reason.
- `enum class` only, with an explicit underlying type for anything stored or serialized. Flag enums use the `TESERRA_ENUM_FLAGS(Type)` helper for bitwise operators.
- Use `std::span`, `std::string_view` for borrowed ranges; never store them beyond the call unless documented.
- Integer types: `u32`, `i32`, `u64`, `usize` from `<teserra/core/types.hpp>`. Use `usize` for sizes and indices into containers, fixed-width types for data formats and GPU structs. Narrowing conversions use `static_cast` (or `narrow<T>()` with a debug check).
- `auto` when the type is obvious from the right-hand side or is an iterator/lambda; spell out types otherwise.

## Functions

- `[[nodiscard]]` on every function returning `Result`, a handle, or a value whose discard is a bug.
- `noexcept` on move operations, destructors (implicit), and functions that cannot fail.
- `constexpr` where it is free.
- Prefer free functions in a namespace over static member functions for stateless helpers.
- Out parameters are avoided; return structs or `Result<T>`.
- Keep functions short enough to read without scrolling (~60 lines is a soft limit).

## Error handling

Follow [ADR-0016](../adr/0016-error-handling-policy.md):

- Bugs → `TESERRA_ASSERT` / `TESERRA_VERIFY` with a message that names the object (`"Buffer '{}' is not mapped"`).
- Recoverable failures → return `Result<T>`; propagate with `TESERRA_TRY`.
- Unrecoverable → `TESERRA_FATAL`.
- Engine code does not `throw` and does not `catch` except at third-party boundaries.
- Log an error once, at the place where it is handled.

## Logging

- Use `TESERRA_LOG_<LEVEL>(category, fmt, args...)` from `<teserra/core/log.hpp>` ([ADR-0011](../adr/0011-logging-spdlog-facade.md)); never `printf`, `std::cout` or spdlog directly. Format strings are `std::format` strings, checked at compile time; types with a `std::formatter` can be logged.
- Engine code uses the `LogCategory` of its module. Games add categories with `teserra::log::registerCategory("AI")` once (keep the returned `LogCategory`, e.g. in a `const` global) instead of logging everything as `Game`.
- `TESERRA_LOG_LEVEL_MIN` (default: Trace in Debug, Info otherwise) removes lower calls at compile time, arguments included; `teserra::log::setLevel(category, level)` filters the rest at runtime before formatting. Arguments of a filtered call are not evaluated, so they must not have side effects the program relies on.
- `Info` for lifecycle events (device created, scene loaded), `Debug` for diagnostics useful during development, `Trace` for per-frame detail (compiled out in Release), `Warn` for recoverable surprises, `Error` for handled failures, `Critical` just before `TESERRA_FATAL`.
- No logging inside per-entity/per-sprite loops above `Trace`.

## Math

- Use aliases and functions from `<teserra/core/math.hpp>` (`Vec3`, `Mat4`, `Quat`, `math::normalize`). Direct `glm::` only inside `engine/core/` ([ADR-0008](../adr/0008-glm-math-behind-aliases.md)).
- Conventions: right-handed, Y-up, radians, column vectors, depth `[0, 1]`, top-left texture origin ([ADR-0024](../adr/0024-coordinate-conventions.md)).
- GPU-visible structs use explicit `alignas` and `static_assert` on size and member offsets.

## Classes

- Rule of zero by default; rule of five when managing a resource. Move-only types delete copy operations explicitly.
- Data members are private unless the type is a plain aggregate (descriptors, components, POD records).
- Interfaces: pure virtual functions, virtual destructor, no data members; implementations are `final` unless designed for further derivation.
- Inheritance depth: interface + implementation. Anything deeper needs an ADR.

## Concurrency

- v1 engine code runs on the main thread. Classes that are main-thread-only say so in their doc comment and assert it in debug builds where cheap.
- Shared mutable state between threads (when introduced) must be documented with its synchronization mechanism next to the member.

## Comments and documentation

- Public API: `///` Doxygen-style comments on every public type and function in `include/`: what it does, preconditions, ownership, thread affinity, error cases.
- Implementation comments explain *why*, not *what*. Link the ADR or issue when a choice is non-obvious (`// See ADR-0024: GL backend flips at present.`).
- `TODO(#123): ...` must reference an issue. Bare `TODO`s fail the lint check.

## Tests

- Every bug fix comes with a test that fails before the fix.
- Test names follow `"<Type>: <behavior>"` with module tags (see [testing strategy](testing-strategy.md)).

## Formatting

- [`.clang-format`](../../.clang-format) in the repository root is the authority: 4-space indentation, 110-column limit, braces on the same line (Attach) and always present, pointer/reference bound to the type (`T* p`, `T& r`), includes grouped as described under [Files and headers](#files-and-headers).
- The reference version is **clang-format 22.1.3** (pinned in CI; the Visual Studio "C++ Clang tools" component and `pip install clang-format==22.1.3` both provide it). Other major versions may format a few constructs differently.
- Run `cmake --build --preset <preset> --target format` before pushing; `--target format-check` (also part of `--target check`) reports unformatted files without changing them. Without CMake: `python tools/scripts/check_format.py [--fix]`. CI runs the same script in the `checks` job.
- Third-party and generated code (`third_party/` directories, build trees) is never formatted.

## Static analysis

- [`.clang-tidy`](../../.clang-tidy) enables `bugprone-*` (except `bugprone-easily-swappable-parameters`), `performance-*`, a selected list of `modernize-*` checks, and `readability-identifier-naming` configured with the rules of [ADR-0021](../adr/0021-naming-conventions.md) (the fixed-width integer aliases are the only exception).
- Run it locally against a configured build, which exports `compile_commands.json`:

  ```bash
  clang-tidy -p build/windows-msvc-debug engine/core/src/*.cpp engine/core/tests/*.cpp
  ```

- Changed files must be clang-tidy clean. A justified suppression uses `// NOLINT(<check>): <reason>` on the line.
