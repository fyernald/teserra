# ADR-0020: Generational handles for engine resources

- **Status:** Accepted
- **Date:** 2026-10-21
- **Related:** [RHI](../architecture/rhi.md#handles-and-resource-lifetime), [ECS](../architecture/ecs.md#entities), [assets](../architecture/assets.md#handles-and-ids), [patterns](../guides/patterns-and-conventions.md)

## Context

RHI resources, entities and assets are created and destroyed at runtime and referenced from many places. Raw pointers dangle silently, make ownership unclear, and cannot cross future boundaries (scripting, serialization, GPU-side indices). Backend object IDs (GL names, `VkImage`) must never leak out of the RHI.

## Decision

- One handle template in `<teserra/core/handle.hpp>`:

  ```cpp
  template <typename Tag>
  class Handle {
  public:
      constexpr Handle() noexcept = default;                   // null handle
      [[nodiscard]] constexpr bool isNull() const noexcept;     // generation == 0
      [[nodiscard]] constexpr u32 index() const noexcept;
      [[nodiscard]] constexpr u32 generation() const noexcept;
      friend constexpr bool operator==(Handle, Handle) noexcept = default;
  private:
      u32 m_index = 0;
      u32 m_generation = 0;   // 0 is reserved for "null"; live slots start at 1
  };
  using BufferHandle = Handle<struct BufferTag>;   // distinct types per resource kind
  ```

- **Layout:** 32-bit index + 32-bit generation (8 bytes, trivially copyable, hashable). Callers use accessors only; the layout is an implementation detail and may be packed later.
- **`HandlePool<T, Tag>`** (core) stores objects in a slot array with a per-slot generation and a **FIFO** free list (FIFO maximizes time before a slot is reused, so stale handles are detected longer). Destroying an object increments the slot generation. A slot whose generation would overflow is retired permanently.
- **Lookup** of a stale or null handle: `TESERRA_ASSERT` in debug; `tryGet()` returns `nullptr` for code that legitimately holds possibly-stale handles.
- **Type safety:** different tags are different types; no implicit conversions between handle kinds.
- **Ownership:** handles are non-owning. The creator (device, world, asset registry) owns the object and defines destruction rules. RAII owners where useful (`rhi::UniqueHandle<T>` that destroys on scope exit) are thin wrappers around a handle + owner reference.
- Entities (`ecs::Entity`) use the same template and layout.

## Alternatives considered

| Option | Why not |
|---|---|
| Raw/`shared_ptr` resources | Dangling or unclear ownership, atomic refcount costs, cannot be serialized or sent to GPU/script boundaries. |
| 32-bit packed handles (e.g. 20-bit index + 12-bit generation) | Half the size, but generation wraps after 4096 reuses (ABA risk) and index limit is tight. Kept as a possible optimization because the layout is hidden. |
| Pointer + generation | Not relocatable, still leaks addresses. |

## Consequences

**Positive**
- Stale handles are detected deterministically in debug builds; no dangling pointers.
- Backends store real objects (GL names, `VkBuffer`) in pools keyed by handle; nothing backend-specific escapes.

**Negative / costs**
- One indirection per access (slot lookup). Negligible compared to the work done per resource.
- 8 bytes per handle; sparse-set dense arrays store entity handles, so memory is slightly higher than EnTT's 32-bit entities.

## Revisit if

- ECS benchmarks show entity handle size matters for cache footprint: switch to a packed 32-bit layout (accessor-only API makes this local).
