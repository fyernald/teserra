# ADR-0017: Shader pipeline: GLSL 450 to SPIR-V, SPIRV-Cross for OpenGL

- **Status:** Proposed (validated by the "SPIR-V shader pipeline" spike before implementation)
- **Date:** 2026-10-21
- **Related:** [RHI architecture](../architecture/rhi.md#shaders), [renderer](../architecture/renderer.md#materials-shaders-and-uniforms), [ADR-0005](0005-explicit-rhi-design.md)

## Context

Shaders should be authored once and run on OpenGL now and Vulkan later. Vulkan consumes SPIR-V. OpenGL 4.5 consumes GLSL source, or SPIR-V through `ARB_gl_spirv` (core in 4.6). The binding model uses `(set, binding)` pairs ([ADR-0005](0005-explicit-rhi-design.md)), which plain OpenGL GLSL cannot express.

## Decision

- Shaders are authored in **GLSL 450, Vulkan dialect** (`#version 450`, `layout(set = S, binding = B)`, `layout(location = N)` on all varyings), one file per stage (`.vert`, `.frag`, later `.comp`).
- An in-tree host tool **`teserra_shaderc`** (in `tools/shaderc/`, linked against **glslang** and **SPIRV-Cross** from vcpkg) runs at build time:
  1. Compiles GLSL to **SPIR-V** (glslang), with `#include` support and engine-defined macros.
  2. **Reflects** resources (SPIRV-Cross) and emits a reflection table (bind groups, binding types, vertex inputs, push-constant block size).
  3. For OpenGL: remaps `(set, binding)` to flat GL binding points using the same deterministic function as the GL backend ([rhi.md](../architecture/rhi.md#binding-model)), and emits **GLSL 450 core** via SPIRV-Cross.
  4. Writes one `.tshader` blob per shader module containing the SPIR-V, the GL GLSL, and reflection data.
- CMake function `teserra_add_shaders(<target> SOURCES ...)` invokes the tool and makes the blobs available (embedded for engine built-ins, files for game assets).
- The OpenGL backend consumes the **emitted GLSL**, not SPIR-V. We do **not** depend on `ARB_gl_spirv` in v1: driver support and quality are inconsistent, it gives no reflection, and debugging GLSL in RenderDoc is easier.
- The RHI `ShaderModuleDesc` takes a `.tshader` blob; each backend picks its payload. Backends never receive raw source from the renderer.
- Until the pipeline lands (iteration 6), the first triangle uses a temporary GLSL-only path (`rhi::temporaryGlslBlob()`, #33) that is removed by the pipeline issue (#44).

## Alternatives considered

| Option | Why not |
|---|---|
| `ARB_gl_spirv` everywhere | Single artifact, but uneven driver support on older Intel/AMD drivers and no reflection. Kept as an optional runtime path to evaluate in the spike. |
| Hand-written GL GLSL + separate Vulkan GLSL | Two sources of truth; drift guaranteed. |
| HLSL via DXC → SPIR-V | Good option for Vulkan/D3D12, but GLSL is the natural first language for an OpenGL-first engine and matches glm conventions. Can be added as a second input language later. |
| Slang | Very attractive (modules, generics, multiple targets); younger toolchain and heavier dependency. Revisit for v2 materials. |
| Runtime compilation only | Enables hot reload but ships glslang + SPIRV-Cross in every game and moves errors to runtime. Runtime compilation can be added for the editor later using the same library code. |

## Consequences

**Positive**
- One source of truth; Vulkan backend consumes the SPIR-V that already exists.
- Shader compile errors surface at build time with file/line.
- Reflection lets the RHI validate pipeline layouts against shaders in debug builds.

**Negative / costs**
- glslang + SPIRV-Cross increase first-build time (mitigated by binary caching).
- A small host tool to maintain.

## Revisit if

- The spike finds SPIRV-Cross GLSL output unsuitable (e.g. binding remap limitations) → fall back to GL GLSL with a binding-convention preprocessor.
- Slang matures and the material system in v2 needs its features.
