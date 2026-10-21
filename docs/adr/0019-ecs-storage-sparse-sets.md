# ADR-0019: ECS storage model: sparse sets

- **Status:** Accepted (validated by the "ECS storage model benchmark" spike)
- **Date:** 2026-10-21
- **Related:** [ECS architecture](../architecture/ecs.md#component-storage), [ADR-0007](0007-custom-ecs.md), [ADR-0020](0020-generational-handles.md)

## Context

The two dominant ECS storage models:

- **Archetypes** (Unity DOTS, flecs, Bevy tables): entities with the same component set share a table; columns are tightly packed. Multi-component iteration is very fast. Adding/removing a component moves the entity (all its components) to another table. Requires an archetype graph, column type erasure, chunk management.
- **Sparse sets** (EnTT, Bevy's sparse-set storage option): one pool per component type: a sparse array indexed by entity index pointing into a dense array of components (plus a dense entity array). Add/remove is O(1) and touches only that component's pool. Single-component iteration is perfectly packed; multi-component views iterate the smallest pool and probe others.

For a solo, learning-focused but performance-conscious engine, implementation complexity and correctness risk weigh heavily. v1 workloads are 2D: thousands to tens of thousands of entities, frequent tag-like component add/remove (gameplay states), and many systems touching 1-3 components.

## Decision

- **Sparse-set storage**, one `ComponentPool<T>` per component type per `World`.
- Sparse arrays are **paged** (fixed-size pages allocated on demand) to avoid allocating the full entity range for rare components.
- Removal uses **swap-and-pop** in the dense arrays; iteration order is therefore unspecified.
- Multi-component views iterate the pool with the **fewest** entities and test membership in the others.
- Optimization roadmap (each step only with benchmark evidence): sorting pools to match another pool's order, then **owning groups** (EnTT-style) for hot component combinations.
- The storage is hidden behind `World`/`View` APIs; switching a pool to a different storage strategy must not change user code.

## Alternatives considered

| Option | Why not (now) |
|---|---|
| Archetypes | Best multi-component iteration, but structural changes are expensive and the implementation (graph edges, column type erasure, chunk lifetime, query caching) is roughly 3x the work and much easier to get subtly wrong. |
| Hybrid (archetypes + sparse sets for volatile components, like Bevy) | Most flexible; most complex. Possible end state, not a starting point. |
| Array-of-structs per entity | No cache benefits, no ECS learning value. |

## Consequences

**Positive**
- Simple, testable implementation; O(1) add/remove; trivially stable component pointers until the next structural change of that pool.
- Easy to reason about for contributors.

**Negative / costs**
- Multi-component views with many components and similar-sized pools are slower than archetype iteration (random access into secondary pools).
- Iteration order changes after removals (document; never rely on order).

## Revisit if

- Benchmarks of representative scenes show multi-component views dominating frame time and groups do not close the gap.
- 3D scenes with large homogeneous entity populations (v2) change the workload profile.
