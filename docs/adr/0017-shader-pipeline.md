# ADR-0017: Shader pipeline: GLSL 450 to SPIR-V, SPIRV-Cross for OpenGL

- **Status:** Accepted, with the amendments from the "SPIR-V shader pipeline" spike ([#36](https://github.com/fyernald/teserra/issues/36), [findings](https://github.com/fyernald/teserra/issues/36#issuecomment-6352921884))
- **Date:** 2026-10-21 (proposed), 2026-10-23 (accepted)
- **Related:** [RHI architecture](../architecture/rhi.md#shaders), [renderer](../architecture/renderer.md#materials-shaders-and-uniforms), [ADR-0005](0005-explicit-rhi-design.md)

## Context

Shaders should be authored once and run on OpenGL now and Vulkan later. Vulkan consumes SPIR-V. OpenGL 4.5 consumes GLSL source, or SPIR-V through `ARB_gl_spirv` (core in 4.6). The binding model uses `(set, binding)` pairs ([ADR-0005](0005-explicit-rhi-design.md)), which plain OpenGL GLSL cannot express.

## Decision

- Shaders are authored in **GLSL 450, Vulkan dialect** (`#version 450`, `layout(set = S, binding = B)`, `layout(location = N)` on all varyings), one file per stage (`.vert`, `.frag`, later `.comp`).
- An in-tree host tool **`teserra_shaderc`** (in `tools/shaderc/`, linked against **glslang** and **SPIRV-Cross** from vcpkg) runs at build time:
  1. Compiles GLSL to **SPIR-V** (glslang, Vulkan client only: the OpenGL client rejects `set` qualifiers), with `#include` support (`GL_GOOGLE_include_directive`) and engine-defined macros. glslang's errors, which name included files and their lines, are reformatted as compiler-style `file(line): error: message` lines.
  2. **Reflects** resources (SPIRV-Cross) and emits a reflection table (bind groups, binding types, vertex inputs, push-constant block size). Uniform block sizes are stored as std140 sizes **rounded up to 16 bytes**, the minimum range GL and Vulkan accept; SPIRV-Cross's declared size is smaller (68 instead of 80 bytes for `{ mat4; float; }`).
  3. For OpenGL: remaps `(set, binding)` to flat GL binding points using the same deterministic function as the GL backend ([rhi.md](../architecture/rhi.md#binding-model)), and emits **GLSL 450 core** via SPIRV-Cross.
  4. Writes one `.tshader` blob per shader module containing the SPIR-V, the GL GLSL, and reflection data.
- CMake function `teserra_add_shaders(<target> SOURCES ...)` invokes the tool and makes the blobs available (embedded for engine built-ins, files for game assets).
- The OpenGL backend consumes the **emitted GLSL**, not SPIR-V. We do **not** depend on `ARB_gl_spirv` in v1. The spike showed, on a current Intel driver:
  - Vulkan SPIR-V with descriptor sets fails to link with an empty log;
  - SPIR-V with patched decorations links, but loses every block and uniform name;
  - and debugging GLSL in RenderDoc is easier.
  It stays a possible later optimization (no GLSL compile at load time), using the SPIR-V already in the blob with its decorations patched to flat bindings.
- **SPIR-V validation** (`spirv-val`, from spirv-tools) is an **opt-in** vcpkg feature that the CI preset enables. spirv-tools takes about 15 minutes to build from source, against about 4.4 minutes for glslang, SPIRV-Cross and SPIRV-Headers together; tests that need `spirv-val` are skipped without it.
- The RHI `ShaderModuleDesc` takes a `.tshader` blob; each backend picks its payload. Backends never receive raw source from the renderer.
- Until the pipeline lands (iteration 6), the first triangle uses a temporary GLSL-only path (`rhi::temporaryGlslBlob()`, #33) that is removed by the pipeline issue (#44).

## Alternatives considered

| Option | Why not |
|---|---|
| `ARB_gl_spirv` everywhere | Single artifact, but uneven driver support and no names: in the spike, raw Vulkan SPIR-V failed to link on Intel with an empty log, and patched SPIR-V lost all names. A later optional runtime path, not v1. |
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
- glslang + SPIRV-Cross + SPIRV-Headers add about 4.4 minutes to a machine's first build (MSVC, from source); later configures restore them from the vcpkg binary cache in about a second.
- A small host tool to maintain.

## Validation (spike #36)

Done with glslang 16.4.0 and SPIRV-Cross 1.4.350.1 on Intel UHD Graphics (OpenGL 4.5), with a sprite shader pair using sets 0, 2 and 3 and an `#include`. Spike code: branch `spike/36-spirv-pipeline`.

- **Remap:** SPIRV-Cross `set_decoration(Binding, set * 8 + binding)` + `unset_decoration(DescriptorSet)` emits GLSL 450 core with `layout(binding = N)`. The driver reported exactly bindings 0, 16, 24 for the uniform blocks and unit 17 for the sampler.
- **Reflection:** names, sets, bindings, kinds, block sizes and vertex inputs (location, type) are all available.
- **Cost:** about 9 ms per shader in glslang and 2-4 ms in SPIRV-Cross (Debug tool). The first compile in a process costs about 0.9 s of glslang setup.

## Revisit if

- The spike finds SPIRV-Cross GLSL output unsuitable (e.g. binding remap limitations) → fall back to GL GLSL with a binding-convention preprocessor.
- Slang matures and the material system in v2 needs its features.
