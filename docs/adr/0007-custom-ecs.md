# ADR-0007: Custom ECS written from scratch

- **Status:** Accepted
- **Date:** 2026-10-21
- **Related:** [ECS architecture](../architecture/ecs.md), [ADR-0019](0019-ecs-storage-sparse-sets.md), [ADR-0020](0020-generational-handles.md)

## Context

Scenes in Teserra are built on an Entity Component System. Mature libraries exist (EnTT, flecs). The project's primary motivation is understanding how engines work, and the ECS is the heart of the gameplay-facing API, so its shape (entity IDs, storage, queries, scheduling, metadata for a future editor and scripting) is something we want to own and evolve.

## Decision

- We write a custom ECS in the `teserra_ecs` module. It depends only on `teserra_core`.
- Development order: **simple and correct first**, then **benchmarked**, then **optimized**. No optimization lands without a benchmark showing the improvement ([memory and performance](../architecture/memory-and-performance.md#benchmarking-policy)).
- Storage model, entity layout and scheduling are specified in [ecs.md](../architecture/ecs.md) and [ADR-0019](0019-ecs-storage-sparse-sets.md).
- Component types are plain C++ types (`std::is_move_constructible`, no base class). Component metadata (name, size, alignment, lifecycle functions) is registered per type to support serialization, the editor and scripting later without RTTI.
- EnTT and flecs are **reference reading**, not dependencies. Their papers and blog posts are cited in ecs.md.

## Alternatives considered

| Option | Why not |
|---|---|
| EnTT | Production quality and fast, but heavy template machinery; adopting it means learning EnTT instead of ECS design, and its API would become Teserra's gameplay API. |
| flecs | Feature-rich (relations, queries DSL, scheduler), C core with C++ wrapper; same "adopting someone else's design" concern, plus a large surface area. |
| No ECS (classic scene graph with GameObject classes) | Simpler for beginners but leads to deep inheritance trees and god objects ([anti-patterns](../guides/patterns-and-conventions.md#anti-patterns)); poor data locality. |

## Consequences

**Positive**
- Full understanding and control of the most user-visible API.
- The ECS can be shaped around the editor/scripting metadata needs from the start.

**Negative / costs**
- Correctness risk (iterator invalidation, structural changes during iteration). Mitigated by extensive unit tests and deferred commands.
- Will be slower than EnTT initially. Acceptable; v1 performance budget is modest ([memory and performance](../architecture/memory-and-performance.md#v1-performance-budget)).

## Revisit if

- After two optimization rounds the ECS still misses the v1 budget by a large margin.
- Maintaining the ECS consumes more than ~25% of capacity over several iterations, crowding out renderer and tooling work.
