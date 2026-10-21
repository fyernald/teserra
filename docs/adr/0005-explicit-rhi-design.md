# ADR-0005: Explicit RHI modeled on Vulkan/D3D12/Metal

- **Status:** Accepted
- **Date:** 2026-10-21
- **Related:** [RHI architecture](../architecture/rhi.md), [ADR-0004](0004-opengl-45-dsa-first-backend.md), [ADR-0017](0017-shader-pipeline.md), [ADR-0020](0020-generational-handles.md)

## Context

The renderer must not be written against OpenGL, otherwise a Vulkan backend means rewriting the renderer. Abstractions shaped like the OpenGL state machine (`bindTexture`, `setBlendMode`, global "current" state) are easy to implement on GL and painful or slow on Vulkan. Abstractions shaped like Vulkan/D3D12/Metal can be implemented on GL with a small amount of emulation.

## Decision

The **Render Hardware Interface (RHI)** is an explicit, descriptor-driven API:

- Objects: `Device`, `Swapchain`, `Buffer`, `Texture`, `Sampler`, `ShaderModule`, `GraphicsPipeline` (all fixed-function state baked in), `BindGroupLayout`, `PipelineLayout`, `BindGroup`, `CommandList`, `Fence`. Render passes are described by a `RenderPassDesc` passed to `CommandList::beginRenderPass`.
- Resources are created from immutable `*Desc` structs and referenced through **generational handles** ([ADR-0020](0020-generational-handles.md)), never raw pointers or backend IDs.
- Work is **recorded** into a `CommandList` (a backend-agnostic command stream) and **submitted** to the device. The OpenGL backend replays the stream at submit time; a Vulkan backend translates it into `VkCommandBuffer`s.
- Resource binding uses **bind groups** (descriptor-set-like) with up to 4 groups per pipeline and a frequency convention (0 = frame, 1 = pass, 2 = material, 3 = draw).
- Synchronization is **implicit inside the RHI** in v1: resource usage is declared at creation (usage flags) and at use sites (render pass attachments, bind groups). Backends derive barriers. Frame pacing uses explicit `Fence`s and a fixed number of frames in flight.
- Nothing backend-specific leaks out: no GL/Vulkan enums, object IDs, handles or headers in `engine/rhi/include/`. A CI check enforces this ([build-and-ci](../guides/build-and-ci.md#layer-and-leak-checks)).
- Backends are selected through a factory (`rhi::createDevice(const DeviceDesc&, platform::Window&)`), compiled in via CMake options.
- A **null backend** (records and validates, renders nothing) exists for headless tests.

## Alternatives considered

| Option | Why not |
|---|---|
| GL-shaped thin wrapper | Fast to write, but every renderer feature would bake in mutable global state; Vulkan backend would require a renderer rewrite. |
| Virtual `CommandList` recorded directly into backend objects | Avoids one translation step on Vulkan, but GL contexts are thread-bound and GL has no command buffers; recording would be restricted to the GL thread. Can be added later as an optimization behind the same API. |
| Exposing explicit barriers in v1 | Most correct for Vulkan, but forces every v1 renderer feature to reason about states that GL ignores. A future render graph can add explicit control without changing the RHI user API. |
| Using an existing RHI library | See [ADR-0004](0004-opengl-45-dsa-first-backend.md): writing the RHI is a deliberate learning goal. |

## Consequences

**Positive**
- The renderer is backend-agnostic from the first triangle.
- Command recording is testable without a GPU (null backend, command stream inspection).
- Pipeline objects make state changes explicit and cacheable on every backend.

**Negative / costs**
- More up-front design than a GL wrapper; first triangle arrives one iteration later.
- The GL backend carries emulation code (replay, binding remap, fences, swapchain blit).
- Implicit synchronization may cost some Vulkan performance until a render graph exists.

## Revisit if

- Profiling shows command stream translation is a measurable cost on Vulkan (then add a direct-recording path).
- The renderer needs fine-grained barrier control before the render graph exists.
