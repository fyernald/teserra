# Repository layout

This is the target layout. Directories appear when the iteration that needs them lands; empty placeholder directories are not committed.

```text
teserra/
├── CMakeLists.txt              # Root project: options, module list, examples/tests toggles
├── CMakePresets.json           # Configure/build/test/workflow presets (see build-and-ci.md)
├── vcpkg.json                  # Dependency manifest (features: tests, benchmarks, profiling, shaders)
├── vcpkg-configuration.json    # Pinned baseline/registries
├── .clang-format, .clang-tidy  # Enforced style and static analysis
├── cmake/                      # CMake helpers: TeserraModule.cmake, TeserraWarnings.cmake,
│                               #   TeserraShaders.cmake, TeserraLayering.cmake
├── engine/                     # The engine, one directory per module (= one CMake target)
│   ├── core/                   # Layer 0: types, assert, log, Result, handles, math, memory, profile
│   ├── platform/               # Layer 1: Platform, Window, Input, FileSystem, Clock, DynamicLibrary
│   ├── assets/                 # Layer 2: asset IDs/handles, registry, loaders, image decoding
│   ├── rhi/                    # Layer 2: RHI interface + backends
│   │   └── backends/
│   │       ├── opengl/         #   teserra_rhi_opengl (only place GL is allowed)
│   │       ├── null/           #   teserra_rhi_null (headless tests)
│   │       └── vulkan/         #   future
│   ├── renderer/               # Layer 3: frame structure, cameras, sprite batcher, materials, debug draw
│   │   └── shaders/            #   engine built-in shaders (GLSL 450, compiled by teserra_shaderc)
│   ├── ecs/                    # Layer 4: World, entities, component pools, views, scheduler primitives
│   ├── scene/                  # Layer 5: Scene, transforms, hierarchy, render extraction, tilemaps
│   └── engine/                 # Layer 6: App, main loop, engine services wiring, public umbrella header
├── editor/                     # Layer 7 (future, v0.3+): editor executable
├── tools/
│   ├── shaderc/                # teserra_shaderc host tool (GLSL -> SPIR-V -> GL GLSL + reflection)
│   └── scripts/                # check_format, check_layers, check_gl_leaks (used by CI)
├── examples/                   # One directory per runnable example (hello, window_input, rhi_triangle, ...)
├── tests/                      # Cross-module integration tests, GPU tests, golden images
│   ├── integration/
│   ├── gpu/
│   └── golden/                 # Snapshot reference images (*.png), reviewed in PRs
├── benchmarks/                 # Catch2 BENCHMARK executables (ecs, core, renderer)
├── docs/                       # This documentation
└── .github/                    # Workflows, issue/PR templates, labels, release config
```

## Module layout

Every module under `engine/` has the same shape:

```text
engine/<module>/
├── CMakeLists.txt                       # teserra_add_module(<module> LAYER <n> DEPENDS ...)
├── include/teserra/<module>/*.hpp       # Public headers: the module's API, nothing else
├── src/*.cpp, src/*.hpp                 # Implementation and private headers
├── src/<platform-or-backend>/           # Only where needed (platform: windows/, posix/, glfw/)
└── tests/*.cpp                          # Unit tests -> executable teserra_<module>_tests
```

Rules:

- **Public headers are the contract.** Anything in `include/` is reviewed as API. Private helpers go in `src/`.
- Includes always use the full path: `#include <teserra/rhi/device.hpp>`. Relative includes (`"../foo.hpp"`) are not allowed.
- A module may only include public headers of modules it `DEPENDS` on (enforced; see [overview](../architecture/overview.md#how-the-layering-is-enforced)).
- One main type per file; file name is the type name in `snake_case` (`command_list.hpp` declares `CommandList`).
- Third-party code is never copied into the tree except generated code (`engine/rhi/backends/opengl/third_party/glad/`), which carries a README with its generator command and is excluded from formatting and lint.

## Where things go

| You are adding... | Put it in |
|---|---|
| A new engine type used by several modules (e.g. a container) | `engine/core/` |
| OS-specific code | `engine/platform/src/<os>/`, behind an interface in `engine/platform/include/` |
| GL code | `engine/rhi/backends/opengl/` only |
| A shader used by the engine | `engine/renderer/shaders/` |
| A component or system shipped with the engine | `engine/scene/` (or `engine/ecs/` if it is ECS infrastructure) |
| A demo of a feature | `examples/<feature_name>/` |
| A test that needs a GPU | `tests/gpu/` |
| A test of one module | `engine/<module>/tests/` |
| A design decision | `docs/adr/` |
| A backlog item, roadmap entry or iteration plan | **GitHub Issues / Project only**, never a file |
