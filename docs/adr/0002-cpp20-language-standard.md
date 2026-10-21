# ADR-0002: C++20 as the language standard

- **Status:** Accepted
- **Date:** 2026-10-21
- **Related:** [coding standards](../guides/coding-standards.md), [ADR-0016](0016-error-handling-policy.md)

## Context

The engine needs modern language features (concepts, `std::span`, designated initializers, `constexpr` improvements, `std::format`, three-way comparison) while remaining buildable with mainstream compilers on Windows today and Linux later. C++23 adds `std::expected` and other conveniences, but support is uneven across standard libraries. C++20 modules are attractive but tooling (CMake, clang-tidy, IDEs, vcpkg ports) is still fragile for a project that wants a one-command contributor build.

## Decision

- The engine is written in **ISO C++20**, set per target with `target_compile_features(<target> PUBLIC cxx_std_20)` and `CXX_EXTENSIONS OFF`.
- **No C++20 modules** for now. Headers + `#pragma once`.
- C++23 library features may be used **only behind an engine wrapper with a C++20 fallback**. Example: `teserra::Result<T>` wraps `tl::expected` today and can alias `std::expected` later ([ADR-0016](0016-error-handling-policy.md)).
- Minimum compilers: **MSVC 19.38 (Visual Studio 2022 17.8)**, **clang-cl 17**, and for the future Linux port **GCC 13** and **Clang 17**. These are the oldest versions that provide `std::format` and the C++20 features listed in the coding standards.
- MSVC is compiled with `/permissive- /Zc:__cplusplus /utf-8`.

## Alternatives considered

| Option | Why not |
|---|---|
| C++17 | Loses `std::span`, concepts, `std::format`, designated initializers, all used heavily by the planned API. |
| C++23 as baseline | `std::expected`, `std::print` and deducing `this` are nice, but GCC/Clang/MSVC support is uneven and would make the Linux port harder. |
| C++20 with modules | Tooling risk is high for contributors and CI. Can be revisited once CMake + MSVC + Clang + clang-tidy handle modules reliably. |

## Consequences

**Positive**
- Expressive, type-safe public API (concepts for component types, `std::span` for uploads, designated initializers for descriptor structs).
- No exotic compiler requirements for contributors.

**Negative / costs**
- Some C++23 conveniences need wrappers.
- Header-based builds compile slower than modules would. Mitigated by keeping public headers small and forward-declaring aggressively.

## Revisit if

- All supported compilers ship complete C++23 `<expected>` support: switch `Result` to `std::expected` (mechanical change).
- CMake + MSVC + Clang + clang-tidy support named modules without workarounds in our CI matrix.
