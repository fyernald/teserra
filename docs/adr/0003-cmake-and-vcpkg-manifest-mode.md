# ADR-0003: CMake with vcpkg manifest mode

- **Status:** Accepted
- **Date:** 2026-10-21
- **Related:** [build and CI guide](../guides/build-and-ci.md), [repository layout](../guides/repository-layout.md)

## Context

Contributors must be able to clone and build with one command. The build must enforce the layer architecture (lower layers cannot depend on upper layers) and must not assume Windows. Dependencies must be pinned so that a build from six months ago still reproduces.

## Decision

- **CMake ≥ 3.28**, target-based only: every module is a target, usage requirements are expressed with `target_*` commands and `PUBLIC`/`PRIVATE`/`INTERFACE` visibility. No global `include_directories`, `add_definitions` or `link_libraries`.
- **CMakePresets.json** defines configure/build/test presets per platform and configuration (`windows-msvc-debug`, `windows-msvc-release`, `windows-clang-debug`, `ci-windows`, `asan`, prepared `linux-gcc-debug`, `linux-clang-debug`). Generator: **Ninja**.
- **vcpkg in manifest mode** (`vcpkg.json` + `vcpkg-configuration.json` with a pinned `builtin-baseline`). The toolchain file is taken from `$env{VCPKG_ROOT}`. Visual Studio 2022 ships vcpkg; standalone install is documented. vcpkg is **not** a git submodule.
- Optional dependencies are vcpkg **features** mapped to CMake options (e.g. `tests`, `benchmarks`, `profiling`).
- Each engine module is declared with a project helper `teserra_add_module(<name> LAYER <n> DEPENDS ...)` that also enforces the layering rule at configure time (see [overview](../architecture/overview.md#how-the-layering-is-enforced)).
- One-command contributor build:

  ```bash
  cmake --workflow --preset windows-msvc-debug
  ```

## Alternatives considered

| Option | Why not |
|---|---|
| Conan | Powerful, but a second tool (Python) to install and learn; vcpkg integrates directly with CMake and Visual Studio. |
| `FetchContent` for everything | Rebuilds dependencies per build tree, slow CI, weak version pinning across transitive deps. Acceptable later for tiny header-only test-only deps if vcpkg lacks them. |
| Vendoring all dependencies | Repository bloat and manual security updates. We vendor only generated code (glad, [ADR-0009](0009-glad-gl-loader.md)). |
| vcpkg as a submodule | Adds ~hundreds of MB clone weight and a submodule step for contributors; `VCPKG_ROOT` is simpler and already present for VS users. |
| Premake / Meson / Bazel | Smaller ecosystem for C++ game-dev libraries and IDE integration. |

## Consequences

**Positive**
- Reproducible dependency versions; CI and contributors use the same baseline.
- Visual Studio, CLion and VS Code (CMake Tools) open the project directly from presets.
- Layering violations fail at configure time, not in review.

**Negative / costs**
- First build compiles dependencies (minutes). Mitigated by vcpkg binary caching in CI and locally.
- Contributors need `VCPKG_ROOT` set; documented in CONTRIBUTING.

## Revisit if

- vcpkg's handling of a critical dependency (e.g. glslang/SPIRV-Cross) becomes a recurring blocker.
- CMake gains first-class package management that makes vcpkg redundant.
