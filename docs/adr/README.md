# Architecture Decision Records

An ADR records one significant decision: the context, what we chose, what we rejected, and what it costs us. ADRs are **immutable once accepted**. To change a decision, write a new ADR that supersedes the old one and update the old one's status line only.

## When an ADR is required

- Adding, replacing or removing a third-party dependency.
- Changing a layer boundary or the allowed dependency direction ([architecture overview](../architecture/overview.md)).
- Changing a public RHI, ECS or asset interface in a way that affects backends or users.
- Deviating from the [pattern catalog](../guides/patterns-and-conventions.md).
- Anything that would surprise a contributor who read the existing docs.

Small, reversible decisions belong in the issue's design note instead. A larger proposal that needs discussion first starts as an **RFC issue** (template: *RFC*) and becomes an ADR when accepted.

## Process

1. Copy [`0000-template.md`](0000-template.md) to `NNNN-short-title.md` (next free number).
2. Status `Proposed`. Open a PR linked to the RFC or design issue.
3. When merged, status becomes `Accepted`. Superseded ADRs get `Superseded by ADR-NNNN`.

## Index

| ADR | Title | Status |
|---|---|---|
| [0001](0001-record-architecture-decisions.md) | Record architecture decisions | Accepted |
| [0002](0002-cpp20-language-standard.md) | C++20 as the language standard | Accepted |
| [0003](0003-cmake-and-vcpkg-manifest-mode.md) | CMake with vcpkg manifest mode | Accepted |
| [0004](0004-opengl-45-dsa-first-backend.md) | OpenGL 4.5 core with DSA as the first graphics backend | Accepted |
| [0005](0005-explicit-rhi-design.md) | Explicit RHI modeled on Vulkan/D3D12/Metal | Accepted |
| [0006](0006-glfw-behind-engine-interfaces.md) | GLFW behind engine-owned Window and Input interfaces | Accepted |
| [0007](0007-custom-ecs.md) | Custom ECS written from scratch | Accepted |
| [0008](0008-glm-math-behind-aliases.md) | glm for math, behind engine aliases | Accepted |
| [0009](0009-glad-gl-loader.md) | glad as the OpenGL loader | Accepted |
| [0010](0010-stb-image-decoding.md) | stb_image for image decoding | Accepted |
| [0011](0011-logging-spdlog-facade.md) | Logging through an engine facade over spdlog | Accepted |
| [0012](0012-testing-framework-catch2.md) | Catch2 v3 as the test and micro-benchmark framework | Accepted |
| [0013](0013-dear-imgui-debug-ui-and-editor.md) | Dear ImGui for debug UI and the future editor | Accepted |
| [0014](0014-scripting-deferred.md) | Scripting deferred to v3-v4 | Accepted |
| [0015](0015-windows-first-platform-abstraction.md) | Windows first, behind a platform abstraction layer | Accepted |
| [0016](0016-error-handling-policy.md) | Error handling: results, assertions, no exceptions in engine code | Accepted |
| [0017](0017-shader-pipeline.md) | Shader pipeline: GLSL 450 to SPIR-V, SPIRV-Cross for OpenGL | Proposed (spike pending) |
| [0018](0018-license-mit.md) | License: MIT | Accepted |
| [0019](0019-ecs-storage-sparse-sets.md) | ECS storage model: sparse sets | Accepted (benchmark validation pending) |
| [0020](0020-generational-handles.md) | Generational handles for engine resources | Accepted |
| [0021](0021-naming-conventions.md) | Naming conventions | Accepted |
| [0022](0022-iteration-and-process-model.md) | Two-week iterations with one headline feature | Accepted |
| [0023](0023-profiling-tracy.md) | Profiling through Tracy behind engine macros | Accepted |
| [0024](0024-coordinate-conventions.md) | Coordinate system and clip-space conventions | Accepted |
