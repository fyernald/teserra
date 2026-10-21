# ADR-0008: glm for math, behind engine aliases

- **Status:** Accepted
- **Date:** 2026-10-21
- **Related:** [ADR-0024](0024-coordinate-conventions.md), [coding standards](../guides/coding-standards.md#math)

## Context

The engine needs vectors, matrices and quaternions that match GLSL semantics and are well tested. Writing a SIMD math library is a fun but non-essential project. We also want the option to replace glm later (e.g. with a custom SIMD library) without editing every file.

## Decision

- **glm** (via vcpkg) is the math implementation, a **`PUBLIC`** dependency of `teserra_core` because engine headers use its types.
- Engine code uses aliases from `<teserra/core/math.hpp>`: `Vec2`, `Vec3`, `Vec4`, `IVec2`, `UVec2`, `Mat3`, `Mat4`, `Quat`, and engine free functions in `teserra::math` for operations used outside core (`normalize`, `dot`, `cross`, `lerp`, `perspective`, `orthographic`, `lookAt`, `inverse`, `toMat4(Quat)`, ...).
- **Direct `glm::` usage is allowed only inside `engine/core/`** (math implementation) and in tests of the math module. A clang-tidy/grep check enforces this outside core.
- glm configuration is fixed project-wide as `PUBLIC` compile definitions on `teserra_core`: `GLM_FORCE_DEPTH_ZERO_TO_ONE`, `GLM_FORCE_RADIANS`, `GLM_ENABLE_EXPERIMENTAL` (only for `gtx/quaternion` helpers we wrap). Mixing configurations across translation units is an ODR hazard, so nothing else may define `GLM_*` macros.
- Conventions (handedness, units, clip-space depth) are specified in [ADR-0024](0024-coordinate-conventions.md).

## Alternatives considered

| Option | Why not |
|---|---|
| Fully wrapping glm in engine classes | Large amount of boilerplate and lost inlining/constexpr benefits for little gain in v1. |
| Using `glm::` everywhere | Fastest, but makes replacement a project-wide rewrite and spreads configuration risk. |
| Custom math library now | Not differentiating for v1; can be revisited for SIMD-heavy 3D work. |
| DirectXMath | Windows-oriented, different conventions from GLSL. |

## Consequences

**Positive**
- Shader and CPU math agree (glm mirrors GLSL).
- Replacement cost is bounded to `engine/core/math*` plus any type-specific code that relies on glm layout.

**Negative / costs**
- Aliases do not hide glm's member functions/operators: a replacement must be source-compatible for those. This is acknowledged; "replaceable" means "replaceable with moderate effort", not "free".

## Revisit if

- 3D work (v2) needs SIMD-friendly layouts or batch math that glm handles poorly.
- Compile times attributable to glm headers become significant (measure with `-ftime-trace`/`/d1reportTime`).
