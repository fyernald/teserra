# Build and CI

How Teserra is built locally and in GitHub Actions. Decisions: [ADR-0003](../adr/0003-cmake-and-vcpkg-manifest-mode.md), [ADR-0002](../adr/0002-cpp20-language-standard.md).

> **Status:** this describes the target setup that the first iteration implements. Commands below become valid as the corresponding issues land.

## Prerequisites (Windows)

- Visual Studio 2022 17.8+ (or Visual Studio 2026) with "Desktop development with C++" (includes MSVC, CMake, Ninja, and vcpkg), **or** standalone MSVC Build Tools + CMake ≥ 3.28 + Ninja.
- vcpkg: either the copy bundled with Visual Studio or a standalone clone. Set `VCPKG_ROOT` to its directory. The Visual Studio developer prompts set it to the bundled copy automatically.
- git on `PATH` (vcpkg fetches the pinned port versions with it).
- Optional: LLVM 17+ for `clang-cl`, `clang-format`, `clang-tidy` (the Visual Studio component "C++ Clang tools for Windows" provides all three).
- A GPU and driver supporting OpenGL 4.5 core.

## One-command build

Run from a **Developer PowerShell for VS** (`-Arch amd64`) or an **x64 Native Tools Command Prompt**, so that MSVC and Ninja are on `PATH`. Visual Studio, VS Code (CMake Tools) and CLion set this environment up themselves when they open the presets.

```bash
cmake --workflow --preset windows-msvc-debug
```

The workflow preset configures, builds and runs tests. If `VCPKG_ROOT` is missing, configuration stops with a one-line message saying so. Individual steps:

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
cmake/TeserraTesting.cmake     # teserra_add_test(), _header_check(), _compile_fail_test(), _abort_test()
cmake/TeserraChecks.cmake      # format, format-check and check targets (tools/scripts/*.py)
cmake/TeserraToolchain.cmake   # toolchain used by the presets: checks VCPKG_ROOT, includes vcpkg
engine/<module>/CMakeLists.txt # one module = one target
tools/shaderc/                 # teserra_shaderc, the build-time shader compiler (host tool)
```

### Options

| Option | Default | Effect |
|---|---|---|
| `TESERRA_BUILD_TESTS` | `ON` (top-level project) | Unit, integration and GPU test executables |
| `TESERRA_BUILD_EXAMPLES` | `ON` (top-level project) | `examples/*` |
| `TESERRA_BUILD_BENCHMARKS` | `OFF` | `benchmarks/*` |
| `TESERRA_WARNINGS_AS_ERRORS` | `OFF` locally, `ON` in CI presets | `/WX` / `-Werror` |
| `TESERRA_WINDOW_BACKEND` | `GLFW` | Window backend of `teserra_platform` (GLFW is the only one in v1) |
| `TESERRA_ENABLE_ASSERTS` | `DEFAULT` (on for Debug/RelWithDebInfo) | `TESERRA_ASSERT` active; `ON` / `OFF` force it for every configuration |
| `TESERRA_ENABLE_PROFILING` | `OFF` | Tracy instrumentation |
| `TESERRA_RHI_OPENGL` | `ON` | OpenGL backend |
| `TESERRA_RHI_VULKAN` | `OFF` | Future Vulkan backend |
| `TESERRA_SHADER_VALIDATION` | `OFF` locally, `ON` in `ci-windows` | SPIR-V validation of `teserra_shaderc`'s output in its tests (vcpkg feature `shader-validation`, SPIRV-Tools; about 15 minutes to build once) |
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

Modules that an option can leave out of the build, such as RHI backends, are named in `OPTIONAL_DEPENDS`: one that exists is linked and layer-checked like `DEPENDS`, and the module's sources see `TESERRA_HAS_<MODULE>` as 1 or 0. The engine uses it for the backends it passes to `rhi::createDevice()`:

```cmake
teserra_add_module(engine LAYER 6
    DEPENDS core platform rhi
    OPTIONAL_DEPENDS rhi_opengl   # TESERRA_HAS_RHI_OPENGL in app.cpp
    ...
)
```

The include check reads `OPTIONAL_DEPENDS` like `DEPENDS`.

### Presets

| Preset | Toolchain | Use |
|---|---|---|
| `windows-msvc-debug` / `windows-msvc-release` | MSVC + Ninja | Default local development |
| `windows-clang-debug` | clang-cl + Ninja | Second compiler, clang-tidy |
| `profile` | MSVC, `RelWithDebInfo`, profiling on | Tracy sessions (added with profiling) |
| `asan` | MSVC or clang-cl, sanitizers on | Memory error hunting (added with sanitizers) |
| `ci-windows` | MSVC Debug, warnings as errors, SPIR-V validation, JUnit output | GitHub Actions |
| `linux-gcc-debug`, `linux-clang-debug` | GCC 13 / Clang 17 + Ninja | Prepared for the Linux port; used by the non-blocking Linux job |

Every preset builds into `build/<preset>/` and exists as a configure, build, test and workflow preset with the same name. Windows presets use the `x64-windows` vcpkg triplet, Linux presets `x64-linux`; presets only show up on their host OS.

Personal tweaks go in `CMakeUserPresets.json` next to `CMakePresets.json`. It is git-ignored and can inherit from the shared presets, for example:

```json
{
  "version": 6,
  "configurePresets": [
    {
      "name": "my-debug",
      "inherits": "windows-msvc-debug",
      "cacheVariables": { "TESERRA_BUILD_EXAMPLES": "OFF" }
    }
  ]
}
```

### vcpkg

- `vcpkg.json` lists dependencies; optional ones are features mapped to CMake options before `project()` (`tests` → catch2 when `TESERRA_BUILD_TESTS` is on, `shader-validation` → spirv-tools when `TESERRA_SHADER_VALIDATION` is on; later `profiling` → tracy). The shader compiler's glslang and SPIRV-Cross are always needed.
- `vcpkg-configuration.json` pins the baseline: the default registry is the `microsoft/vcpkg` git repository at a fixed commit, so every machine resolves the same port versions whether it uses the Visual Studio copy of vcpkg or a standalone clone of any age. Updating it is a separate PR titled `build: bump vcpkg baseline` with a full CI run.
- Presets reach vcpkg through `cmake/TeserraToolchain.cmake`, which checks `VCPKG_ROOT` and then includes `$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake`.
- Never add a dependency without an ADR ([ADR index](../adr/README.md#when-an-adr-is-required)).

Pinned baseline: [`9e593bb`](https://github.com/microsoft/vcpkg/commit/9e593bb18ea69cc5095e012465dcd675a822ed0d) (vcpkg release `2026.07.29`). Resolved versions:

| Port | Version | Used by | Feature |
|---|---|---|---|
| `glm` | 1.0.3 | core (math, iteration 2) | - |
| `spdlog` | 1.17.0#1 | core (logging, iteration 2) | - |
| `fmt` | 12.2.0#1 | transitively through spdlog | - |
| `tl-expected` | 1.3.1 | core (`Result`, iteration 2) | - |
| `glfw3` | 3.4#1 | platform (windows, OpenGL context, iteration 3) | - |
| `catch2` | 3.15.3 | tests | `tests` |
| `glslang` | 16.4.0 | `teserra_shaderc` (GLSL to SPIR-V, iteration 6) | - |
| `spirv-cross` | 1.4.350.1 | `teserra_shaderc` (reflection, GLSL for OpenGL, iteration 6) | - |
| `spirv-tools` | 1.4.350.1 | `teserra_shaderc` tests (SPIR-V validation) | `shader-validation` |
| `spirv-headers` | 1.4.350.1 | transitively through spirv-tools | `shader-validation` |

## Shader compiler

`teserra_shaderc` (`tools/shaderc/`) compiles shaders at build time ([ADR-0017](../adr/0017-shader-pipeline.md)). It is a host tool built with the engine; games do not link it. It compiles GLSL 450 (Vulkan dialect) to SPIR-V with glslang, reflects it and emits GLSL for OpenGL with SPIRV-Cross, and writes one [`.tshader` blob](../architecture/rhi.md#tshader-blobs) per shader. `teserra_add_shaders()` will run it from CMake (#41). The tool links `teserra_rhi` for the blob format, so `teserra_rhi` itself cannot compile shaders with it.

```text
teserra_shaderc <input.vert|.frag|.comp> -o <output.tshader> [-I <dir>]... [-D <name>[=<value>]]...
                [--diagnostics msvc|gcc]
```

- The stage comes from the extension. `#include "x"` is resolved next to the including file, then in the `-I` directories; `#include <x>` only in the `-I` directories. Shaders need no `#extension` line for it.
- Every shader sees `TESERRA_SHADER` and its stage macro (`TESERRA_STAGE_VERTEX`, `_FRAGMENT`, `_COMPUTE`) defined to 1, plus the `-D` macros.
- Errors and warnings go to stderr with absolute paths, as `file(line): error: message` on Windows (Visual Studio) and `file:line: error: message` elsewhere; `--diagnostics` picks one. Errors in an included file name that file.
- Resources the RHI cannot bind are errors that name the resource, without a line (SPIR-V has no line numbers): sets above 3 or bindings above 7, two resources at one binding, resource arrays, separate textures and samplers (use `sampler2D`), storage images, push constants (#135), and vertex inputs that are matrices or not 32-bit floats or integers.
- Exit codes: 0 on success, 1 when the shader has errors, 2 for usage and I/O errors. On failure the output file is removed, so the build never sees a stale one.
- The blob's SPIR-V is SPIR-V 1.5 for Vulkan 1.2. With `TESERRA_SHADER_VALIDATION`, the tests (`teserra_shaderc_tests`, CTest label `shaderc`) check it with SPIRV-Tools' validator, the checks of `spirv-val`; without it those tests are skipped.

## Layer and leak checks

Run locally with `cmake --build --preset <preset> --target check` (or one of `layer-check`, `leak-check`, `format-check`), or run the scripts directly with Python 3; CI runs the scripts in the `checks` job. They print `path:line:col: error: message [rule]`, which CI turns into annotations on the pull request.

| Check | Tool | Fails when |
|---|---|---|
| Layer rule at configure time | `teserra_add_module(... LAYER n DEPENDS ...)` | A module depends on a module in the same or a higher layer |
| Include rule (`layer-include`) | `tools/scripts/check_layers.py` | A module includes `<teserra/<x>/...>` and `x` is neither the module itself nor a **direct** `DEPENDS` entry (transitive dependencies do not count). `<teserra/test/...>` is allowed only under a module's `tests/` |
| Backend leak (`gl-leak`, `vulkan-leak`) | `tools/scripts/check_gl_leaks.py` | GL names (`GL_*`, `gl[A-Z][a-z]*`, `GLuint`-style types, `GLAD_*`) or headers (`glad/`, `GL/`, `KHR/`, ...) outside `engine/rhi/backends/opengl/`; Vulkan names (`Vk*`, `vk*`) or headers outside `engine/rhi/backends/vulkan/` |
| Platform leak (`glfw-leak`, `win32-leak`) | same script | GLFW names or headers outside `engine/platform/src/glfw/`; Win32 headers (`<windows.h>`, ...) outside Windows-only platform files (a `windows/` directory or a `*_windows` file under `engine/platform/`) |
| glm leak (`glm-leak`) | same script | `glm::`, `GLM_*` or `<glm/...>` outside `engine/core/` |
| TODO hygiene (`todo-issue`) | same script | `TODO` without `(#issue)` in a C++ comment or a CMake file |
| Format | `tools/scripts/check_format.py` (`clang-format --dry-run --Werror` per file) | Any file differs from the format |
| Static analysis | `clang-tidy` (config in repo) | New warnings in changed files |

Names are matched in code only: comments and string literals do not count, so `"glClear"` in a test or "GLFW" in a doc comment is fine; includes are matched in `#include` and `__has_include`. Third-party and generated code (`third_party/`, `generated/`, e.g. glad) and git-ignored files are skipped. Reviewed exceptions are listed with their reason in `EXCEPTIONS` of `check_gl_leaks.py` (today: `<windows.h>` in `engine/core/src/assert.cpp`, see ADR-0015). Each script has unit tests in `tools/scripts/tests/` (CTest `tools.scripts`); both scripts check the whole repository in well under a second.

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

The `windows-msvc` job runs on `windows-latest`: it runs `vcvars64.bat` of the newest Visual Studio on the runner (found with `vswhere`) and exports the resulting environment, uses the runner's preinstalled vcpkg as `VCPKG_ROOT`, then configures, builds and tests with the `ci-windows` preset. A failed Catch2 check is annotated on its source line through a problem matcher (`.github/problem-matchers/catch2.json`); CTest's JUnit report (`build/ci-windows/junit.xml`) becomes the `windows-msvc tests` check run with a summary and is uploaded as an artifact. A newer push to the same branch or PR cancels the run in progress. Third-party actions are pinned to a commit SHA.

**Caching.** vcpkg's `files` binary-cache provider writes compiled packages to a directory that `actions/cache` restores before configuring and saves right after (also when the build or tests fail). The key is triplet + runner image version (which fixes the compiler) + hash of `vcpkg.json` and `vcpkg-configuration.json`; a key miss falls back to the newest cache of the same triplet, so a manifest change only rebuilds the packages that changed. vcpkg's former GitHub Actions cache provider (`x-gha`) no longer exists, and a GitHub Packages NuGet feed would need package write permissions and NuGet setup for the same result.

### Branch protection (manual repository setting)

`main` requires: PR before merge, status checks `windows-msvc` and `checks` passing, linear history (squash merge), no force pushes. The maintainer may self-merge after the checklist is satisfied.

To set it up (Settings → Branches → Add branch ruleset, or a classic branch protection rule for `main`):

1. Target branch `main`; enforcement **Active**.
2. **Require a pull request before merging** (required approvals: 0, so the maintainer can self-merge).
3. **Require status checks to pass**, with "require branches to be up to date"; add `windows-msvc` and `checks` (they appear in the list after the workflow has run once).
4. **Require linear history**; under Settings → General → Pull Requests allow only **squash merging**.
5. **Block force pushes** and **restrict deletions**.

## Profiling builds

```bash
cmake --preset profile
cmake --build --preset profile --target <example>
```

Run the example and connect the Tracy profiler whose version matches the vcpkg `tracy` port (documented in `vcpkg.json` comments when the port is added).
