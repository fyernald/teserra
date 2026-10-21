# ADR-0004: OpenGL 4.5 core with DSA as the first graphics backend

- **Status:** Accepted
- **Date:** 2026-10-21
- **Related:** [ADR-0005](0005-explicit-rhi-design.md), [RHI architecture](../architecture/rhi.md), [ADR-0009](0009-glad-gl-loader.md), [risks](../risks-and-open-questions.md)

## Context

v1 is a 2D engine for indie developers on Windows. The first backend should be quick to bring up and debug, run on nearly every desktop GPU from the last ten years, and still map cleanly onto the explicit RHI ([ADR-0005](0005-explicit-rhi-design.md)) that a Vulkan backend will implement later.

## Decision

- The first backend is **OpenGL 4.5 core profile**, using **Direct State Access (DSA)** exclusively (`glCreate*`, `glNamed*`, `glTexture*`, `glVertexArray*`). Bind-to-edit is not used.
- Required beyond 4.5 core: **`KHR_debug`** (core in 4.3, used for debug output and object labels). Optional, queried at runtime: `ARB_gl_spirv`, `EXT_texture_filter_anisotropic`.
- The backend lives entirely in `engine/rhi/backends/opengl/` and is the **only** code allowed to include GL headers or use GL types, enums or object names.
- Clip control: the backend calls `glClipControl` at device creation so that engine-wide conventions ([ADR-0024](0024-coordinate-conventions.md)) hold.
- **macOS is out of scope.** macOS caps OpenGL at 4.1 (no DSA, no `glClipControl`, no compute, deprecated since 10.14). A future macOS path goes through Vulkan + MoltenVK or a Metal backend, not through OpenGL.

## Alternatives considered

| Option | Why not (now) |
|---|---|
| Vulkan first | Correct long-term target, but bring-up cost (instance, device, swapchain, memory, synchronization) would delay the first demo by months for a 5-10 h/week schedule. The RHI is designed so Vulkan comes later without rewrites. |
| OpenGL 3.3 core | Runs on more old hardware, but no DSA, no `glClipControl`, no `ARB_gl_spirv` path; the backend would need bind-to-edit state juggling that fights the explicit RHI. |
| OpenGL 4.1 (macOS compatible) | Same downsides as 3.3 plus a deprecated platform. |
| Direct3D 11/12 | Windows-only, contradicts the "not Windows-only by design" goal for the first and only backend. |
| bgfx / sokol_gfx / WebGPU (Dawn/wgpu) | Solid libraries, but writing the RHI is a core learning goal of the project, and wrapping another abstraction adds a layer we do not control. |

## Consequences

**Positive**
- Fast bring-up, excellent debuggers (RenderDoc, Nsight), wide hardware support on Windows and Linux.
- DSA maps naturally to "create object from a descriptor" RHI calls.

**Negative / costs**
- The GL backend must **emulate** explicit concepts: command lists are recorded and replayed, `(set, binding)` pairs are remapped to flat GL binding points, fences use `glFenceSync`, and render pass load/store ops become clears/invalidates. These gaps are listed in [rhi.md](../architecture/rhi.md#opengl-backend-mapping) and in the risks document.
- GL driver quality varies by vendor; we test on at least two vendors before v0.1 if testers allow.
- No macOS support until a Vulkan/Metal backend exists.

## Revisit if

- A significant share of testers cannot run GL 4.5 (then consider a 4.3 fallback that drops DSA in a few code paths, isolated in the backend).
- The Vulkan backend reaches parity and GL maintenance becomes a drag (then GL may become "legacy, best effort").
