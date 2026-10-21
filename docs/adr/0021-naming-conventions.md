# ADR-0021: Naming conventions

- **Status:** Accepted
- **Date:** 2026-10-21
- **Related:** [coding standards](../guides/coding-standards.md), [repository layout](../guides/repository-layout.md)

## Context

Consistent naming makes a codebase readable by strangers, which is the point of an open-source engine. The rules must be mechanical enough for clang-tidy's `readability-identifier-naming` to enforce.

## Decision

| Element | Convention | Example |
|---|---|---|
| Namespaces | `lower_case`, rooted at `teserra` | `teserra::rhi`, `teserra::ecs` |
| Types (class, struct, enum, alias, concept) | `PascalCase` | `CommandList`, `BufferDesc`, `ComponentType` |
| Functions and methods | `camelCase`, verb first | `createBuffer()`, `beginRenderPass()` |
| Variables, parameters | `camelCase` | `frameIndex`, `vertexCount` |
| Private/protected data members | `m_camelCase` | `m_slots` |
| Public struct fields (POD/desc structs) | `camelCase`, no prefix | `desc.sizeInBytes` |
| Static/global constants, `constexpr` values | `kPascalCase` | `kMaxFramesInFlight` |
| Enumerators (`enum class` only) | `PascalCase` | `Format::Rgba8Unorm` |
| Template parameters | `PascalCase` | `template <typename Component>` |
| Macros | `TESERRA_UPPER_SNAKE` | `TESERRA_ASSERT` |
| Booleans | `is`/`has`/`should`/`can` prefix | `isVisible`, `hasDepth` |
| Interfaces | No `I` prefix; implementations are qualified by backend | `Window` / `GlfwWindow`, `Device` / `GlDevice` |
| Descriptor structs | `*Desc` suffix | `TextureDesc`, `GraphicsPipelineDesc` |
| Handles | `*Handle` suffix | `TextureHandle` |
| Files | `snake_case.hpp` / `snake_case.cpp`, one main type per file | `command_list.hpp` |
| Public headers | `engine/<module>/include/teserra/<module>/...` | `#include <teserra/rhi/device.hpp>` |
| CMake targets | `teserra_<module>` with alias `Teserra::<Module>` | `teserra_rhi` / `Teserra::Rhi` |
| CMake options | `TESERRA_<UPPER_SNAKE>` | `TESERRA_BUILD_TESTS` |
| Test cases | `"<Type>: <behavior>"` with tags `[module]` | `"HandlePool: stale handle is rejected" [core]` |

Abbreviations are written as words in `PascalCase`/`camelCase` (`GlDevice`, `RhiBackend`, `UvRect`), except well-established two-letter acronyms in type names where readability suffers (`ID` is written `Id`: `AssetId`).

Fixed-width integer aliases from `<teserra/core/types.hpp>` are preferred in engine code: `u8 u16 u32 u64 i8 i16 i32 i64 f32 f64 usize`.

## Alternatives considered

| Option | Why not |
|---|---|
| `snake_case` everything (standard library style) | Reads well, but blends engine types with `std::` names and is less common in game-engine codebases contributors know (Unreal, Godot use PascalCase types). |
| Google style (`PascalCase` functions, trailing `_` members) | `PascalCase` functions blur type/function distinction in a type-heavy API. |
| Hungarian notation | Noise, outdated. |

## Consequences

**Positive**
- Enforceable by clang-tidy; predictable API for users.

**Negative / costs**
- glm and std types do not follow these rules; that is accepted at the boundary (aliases in core hide most of glm).

## Revisit if

- Never lightly: renaming conventions after v0.1 is a breaking change for every user.
