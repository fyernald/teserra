# ADR-0009: glad as the OpenGL loader

- **Status:** Accepted
- **Date:** 2026-10-21
- **Related:** [ADR-0004](0004-opengl-45-dsa-first-backend.md), [RHI architecture](../architecture/rhi.md#opengl-backend-mapping)

## Context

OpenGL functions beyond 1.1 must be loaded at runtime. The loader must expose exactly the API we target (4.5 core + a few extensions) so that accidental use of compatibility-profile or newer functions fails to compile.

## Decision

- We use **glad 2** to generate a loader for **OpenGL 4.5 core** plus extensions `GL_KHR_debug`, `GL_ARB_gl_spirv`, `GL_EXT_texture_filter_anisotropic`.
- The generated sources are **vendored** in `engine/rhi/backends/opengl/third_party/glad/` together with a `README.md` containing the exact generator command and version, so the loader can be regenerated reproducibly.
- The loader is compiled into `teserra_rhi_opengl` as a `PRIVATE` dependency (the C library `teserra_glad`, which only the backend and its tests link). Its headers are never visible outside the OpenGL backend. It is generated in MX mode: the device owns its table of GL function pointers, so there is no global loader state.
- Function pointers are loaded through `platform::OpenGlContext::getProcAddress`, so the backend does not depend on GLFW directly.

## Alternatives considered

| Option | Why not |
|---|---|
| glad from vcpkg | Port features for API version and extension selection are coarser than the generator; a generated, reviewed file is easier to audit and pin. Switch if the port offers exact selection. |
| GLEW | Loads everything including compatibility profile; global init semantics; less control. |
| glbinding / gl3w | Fine alternatives; glad is the de-facto standard with the best generator. |

## Consequences

**Positive**
- Using a function outside GL 4.5 core (or the listed extensions) is a compile error.
- No runtime dependency, no global loader state visible to the engine.

**Negative / costs**
- Generated code in the repository (excluded from clang-format/clang-tidy and from layer checks).

## Revisit if

- We need a different GL version or many more extensions (regenerate; the ADR stays valid).
