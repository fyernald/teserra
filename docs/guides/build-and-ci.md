# Build and CI

How Teserra is built locally and in GitHub Actions. Decisions: [ADR-0003](../adr/0003-cmake-and-vcpkg-manifest-mode.md), [ADR-0002](../adr/0002-cpp20-language-standard.md).

> **Status:** this describes the target setup that the first iteration implements. Commands below become valid as the corresponding issues land.

## Prerequisites (Windows)

- Visual Studio 2022 17.8+ with "Desktop development with C++" (includes MSVC, CMake, Ninja, and vcpkg), **or** standalone MSVC Build Tools + CMake ≥ 3.28 + Ninja.
- vcpkg: either the copy bundled with Visual Studio or a standalone clone. Set `VCPKG_ROOT` to its directory.
- Optional: LLVM 17+ for `clang-cl`, `clang-format`, `clang-tidy`.
- A GPU and driver supporting OpenGL 4.5 core.

## One-command build

```bash
cmake --workflow --preset windows-msvc-debug
```

The workflow preset configures, builds and runs tests. Individual steps:

```bash
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
```

The first configure builds dependencies through vcpkg (several minutes). Later builds reuse the vcpkg binary cache.

## CMake structure

```text
CMakeLists.txt                 # project(), options, add_subdirectory(engine/...), examples, tests
cmake/TeserraModule.cmake      # teserra_add_module(), teserra_link_third_party(), layer check
cmake/TeserraWarnings.cmake    # teserra_set_warnings(<target>) per compiler
cmake/TeserraShaders.cmake     # teserra_add_shaders(<target> SOURCES ...)
cmake/TeserraTesting.cmake     # teserra_add_test(<name> SOURCES ... DEPENDS ...) + catch_discover_tests
engine/<module>/CMakeLists.txt # one module = one target
```

### Options

| Option | Default | Effect |
|---|---|---|
| `TESERRA_BUILD_TESTS` | `ON` (top-level project) | Unit, integration and GPU test executables |
| `TESERRA_BUILD_EXAMPLES` | `ON` (top-level project) | `examples/*` |
| `TESERRA_BUILD_BENCHMARKS` | `OFF` | `benchmarks/*` |
| `TESERRA_WARNINGS_AS_ERRORS` | `OFF` locally, `ON` in CI presets | `/WX` / `-Werror` |
| `TESERRA_ENABLE_ASSERTS` | `ON` for Debug/RelWithDebInfo | `TESERRA_ASSERT` active |
| `TESERRA_ENABLE_PROFILING` | `OFF` | Tracy instrumentation |
| `TESERRA_RHI_OPENGL` | `ON` | OpenGL backend |
| `TESERRA_RHI_VULKAN` | `OFF` | Future Vulkan backend |
| `TESERRA_WITH_DEBUG_DRAW` | `ON` | Debug draw API compiled in |
| `TESERRA_ENABLE_SANITIZERS` | `OFF` | ASan (MSVC/Clang), UBSan (Clang/GCC) |

When Teserra is consumed as a subproject or installed package, tests/examples default to `OFF`.

Each option is added together with the feature it controls; until then, CMake reports a manually specified option as unused.

### Modules and the layer check

Every engine module is declared in its own `engine/<module>/CMakeLists.txt`:

```cmake
teserra_add_module(core LAYER 0
    PUBLIC_HEADERS include/teserra/core/types.hpp
    SOURCES src/version.cpp
)
teserra_link_third_party(core PRIVATE spdlog::spdlog)
```

`teserra_add_module()` creates the static library `teserra_<name>` and the alias `Teserra::<Name>`, applies `cxx_std_20` (no extensions, no module scanning) and the warning set from `teserra_set_warnings()`, makes `include/` the public include directory (a `HEADERS` file set) and `src/` the private one, and links every `DEPENDS` module `PUBLIC`. Modules are added in layer order from the root `CMakeLists.txt`. Configuration fails when a dependency is unknown or not in a strictly lower layer:

```text
Layer violation: module 'core' (layer 0) must not depend on module 'platform' (layer 1). ...
```

The rule is tested by configuring the fixture project in `tests/cmake/layer_rule/` (CTest label `cmake`).

### Presets

| Preset | Toolchain | Use |
|---|---|---|
| `windows-msvc-debug` / `windows-msvc-release` | MSVC + Ninja | Default local development |
| `windows-clang-debug` | clang-cl + Ninja | Second compiler, clang-tidy |
| `profile` | MSVC, `RelWithDebInfo`, profiling on | Tracy sessions |
| `asan` | MSVC or clang-cl, sanitizers on | Memory error hunting |
| `ci-windows` | MSVC, warnings as errors, JUnit output | GitHub Actions |
| `linux-gcc-debug`, `linux-clang-debug` | GCC 13 / Clang 17 + Ninja | Prepared for the Linux port; used by the non-blocking Linux job |

Personal tweaks go in `CMakeUserPresets.json` (git-ignored).

### vcpkg

- `vcpkg.json` lists dependencies; optional ones are features (`tests` → catch2, `profiling` → tracy, `shaders` → glslang + spirv-cross).
- `vcpkg-configuration.json` pins the `builtin-baseline`. Updating it is a separate PR titled `build: bump vcpkg baseline` with a full CI run.
- Never add a dependency without an ADR ([ADR index](../adr/README.md#when-an-adr-is-required)).

## Layer and leak checks

Run locally with `cmake --build --preset <preset> --target check` (and in CI):

| Check | Tool | Fails when |
|---|---|---|
| Layer rule at configure time | `teserra_add_module(... LAYER n DEPENDS ...)` | A module depends on a module in the same or a higher layer |
| Include rule | `tools/scripts/check_layers.py` | A module includes `<teserra/<x>/...>` without depending on `x` |
| Backend leak | `tools/scripts/check_gl_leaks.py` | GL identifiers/headers appear outside `engine/rhi/backends/opengl/`; GLFW/Win32 headers outside `engine/platform/src/` |
| glm leak | same script | `glm::` appears outside `engine/core/` |
| TODO hygiene | same script | `TODO` without `(#issue)` |
| Format | `clang-format --dry-run --Werror` | Any file differs from the format |
| Static analysis | `clang-tidy` (config in repo) | New warnings in changed files |

## GitHub Actions

Workflows live in `.github/workflows/`.

| Workflow / job | Trigger | Blocking | Content |
|---|---|---|---|
| `ci.yml` → `windows-msvc` | push to `main`, PRs | **Yes** | Configure (`ci-windows`), build all targets, run unit + integration tests, upload JUnit |
| `ci.yml` → `checks` | push, PRs | **Yes** | format check, layer/leak checks |
| `ci.yml` → `linux-portable` | push, PRs | No (until the Linux port) | Build and test `core`, `ecs`, `rhi` + null backend on `ubuntu-latest` with GCC 13 and Clang 17 |
| `ci.yml` → `sanitizers` | push to `main`, nightly, PRs labeled `needs-asan` | No → Yes once stable | `asan` preset, unit tests |
| `ci.yml` → `clang-tidy` | PRs | Yes for changed files | clang-tidy on changed translation units |
| `gpu.yml` → `gpu-software` | push to `main`, PRs touching `engine/rhi`, `engine/renderer`, `tests/gpu` | Yes once introduced | GPU tests on a software rasterizer (approach decided by the headless GPU testing spike) |
| `bench.yml` | manual, weekly | No | Benchmarks in Release, results as artifact |
| `release.yml` | tag `v*` | - | Build release artifacts, attach to GitHub Release ([release process](../process/release-process.md)) |

Caching: vcpkg binary cache (the provider is chosen in the CI setup issue; the GitHub Actions cache or GitHub Packages NuGet feed are both viable) keyed by triplet + baseline + `vcpkg.json` hash.

### Branch protection (manual repository setting)

`main` requires: PR before merge, status checks `windows-msvc` and `checks` passing, linear history (squash merge), no force pushes. The maintainer may self-merge after the checklist is satisfied.

## Profiling builds

```bash
cmake --preset profile
cmake --build --preset profile --target <example>
```

Run the example and connect the Tracy profiler whose version matches the vcpkg `tracy` port (documented in `vcpkg.json` comments when the port is added).
