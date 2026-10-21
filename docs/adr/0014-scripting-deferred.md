# ADR-0014: Scripting deferred to v3-v4

- **Status:** Accepted
- **Date:** 2026-10-21
- **Related:** [editor and scripting (future)](../architecture/editor-and-scripting-future.md), [ECS](../architecture/ecs.md#component-types-and-metadata)

## Context

Scripting (Lua, C#, Wren, AngelScript, or hot-reloaded C++) is a large subsystem: bindings, lifetime rules, debugging, and an API that must be stable. Building it before the C++ API is stable doubles the API churn.

## Decision

- No scripting language in v1 or v2. Target **v3-v4**.
- Gameplay in v1/v2 is written in C++ against the public engine API.
- The architecture must **not block** scripting. Concretely, v1 must provide:
  1. **Component metadata** registered per type (name, size, alignment, lifecycle functions, optional field descriptors) without RTTI, so bindings and serialization can be generated later ([ecs.md](../architecture/ecs.md#component-types-and-metadata)).
  2. **Systems as plain callables** with declared data access, so a script-defined system can be registered the same way.
  3. **No hidden global state** that a scripting VM would need to reach into; engine services are reachable through explicit context objects.
  4. **Stable handles** (entities, assets) that can cross a language boundary as integers.
- A dynamic-library loading primitive exists in the platform layer (unscheduled backlog) for future C++ hot reload.

## Alternatives considered

| Option | Why not |
|---|---|
| Lua (sol2) in v1 | Popular with indie devs, but locks the API early and adds binding maintenance during the most unstable phase. |
| C# (Mono/.NET hosting) | Very attractive for indie users; much heavier integration. Candidate for v3+. |
| C++ hot reload in v1 | Valuable but fragile (ABI, state migration); best done once the module boundaries are stable. |

## Consequences

**Positive**
- API stabilization happens in one language first.

**Negative / costs**
- Some indie users expect scripting; v1 targets developers comfortable with C++.

## Revisit if

- Feedback rounds show C++-only gameplay is the main adoption blocker, and the public API has been stable for two minor versions.
