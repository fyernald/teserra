# Teserra

**A modern C++20 game engine for indie developers.** 2D first, built on an architecture that is ready for 3D, Vulkan and Linux from day one.

[![CI](https://github.com/fyernald/teserra/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/fyernald/teserra/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
![Status: pre-alpha](https://img.shields.io/badge/status-pre--alpha-orange.svg)

> **Status: pre-alpha, planning and foundations.** There is no usable engine yet. The documentation, architecture decisions and backlog are in place; code arrives iteration by iteration. Follow progress on the **[GitHub Project](https://github.com/users/fyernald/projects/3)**.

## Vision

Teserra aims to be an engine you can **read, understand and ship games with**:

- **Approachable for indie developers:** a small, documented C++ API: create an app, spawn entities with plain-data components, write systems as functions, run.
- **Understandable inside:** core systems (RHI, renderer, ECS, memory utilities) are written from scratch and documented with architecture docs and decision records. Boring solved problems (windowing, image decoding, logging) use well-known libraries behind engine-owned interfaces.
- **Built to grow:** v1 is a 2D engine, but transforms, cameras, materials, vertex formats and render targets are 3D-capable; the renderer sits on a Vulkan-style RHI; the platform layer keeps Windows specifics in one place.

## Feature goals

| Version | Goals |
|---|---|
| **v0.1** (first public release) | OpenGL 4.5 backend behind an explicit RHI, build-time shader pipeline (GLSL → SPIR-V), sprites with batching, 2D camera, custom ECS with scenes and systems, asset system, tilemaps (Tiled import), debug draw, a demo game, Windows x64 |
| v0.2 | Dear ImGui debug tools, Linux support, audio |
| v0.3 → v1.0 | Editor, 2D physics integration, API stabilization |
| v2.0 | Vulkan backend, 3D renderer |
| v3.0+ | Scripting |

The detailed roadmap, iterations and backlog live **only** in the [GitHub Project](https://github.com/users/fyernald/projects/3) and [issues](https://github.com/fyernald/teserra/issues).

## Quickstart

Requirements: Windows 10/11 x64, Visual Studio 2022 17.8+ or 2026 with "Desktop development with C++" (or MSVC Build Tools + CMake 3.28 + Ninja + vcpkg), and git. From a **Developer PowerShell for VS**:

```bash
git clone https://github.com/fyernald/teserra.git
cd teserra
cmake --workflow --preset windows-msvc-debug
build/windows-msvc-debug/examples/hello/teserra_hello
```

The workflow preset configures, builds and runs the tests; the first run also builds the dependencies through vcpkg, which takes a few minutes. `teserra_hello` prints the engine version. Details, other presets and IDE setup: [build and CI](docs/guides/build-and-ci.md).

## A taste of the intended API

```cpp
#include <teserra/teserra.hpp>
using namespace teserra;

struct Player { f32 speed = 6.0f; };

void movePlayer(SystemContext& ctx) {
    const Vec2 dir = ctx.input().axis2D(Key::A, Key::D, Key::S, Key::W);
    for (auto [entity, transform, player] : ctx.view<Transform, const Player>())
        transform.position += Vec3{dir * player.speed * ctx.time().deltaSeconds(), 0.0f};
}

int main() {
    App app{AppConfig{.title = "Hello Teserra", .windowSize = {1280, 720}}};
    Scene& level = app.scenes().create("level");

    Entity hero = level.spawn();
    level.add<Transform>(hero);
    level.add<SpriteRenderer>(hero, {.texture = app.assets().load<Texture>("assets://hero.png")});
    level.add<Player>(hero);

    Entity camera = level.spawn();
    level.add<Transform>(camera);
    level.add<Camera>(camera, {.projection = Orthographic{.height = 18.0f}});

    level.systems().add(Phase::Update, "move_player", movePlayer).reads<Player>().writes<Transform>();
    return app.run(level);
}
```

Full example and design rationale: [public API](docs/architecture/public-api.md).

## Documentation

- [Documentation index](docs/README.md)
- [Architecture overview](docs/architecture/overview.md) · [RHI](docs/architecture/rhi.md) · [Renderer](docs/architecture/renderer.md) · [ECS](docs/architecture/ecs.md)
- [Architecture Decision Records](docs/adr/README.md)
- [Development process](docs/process/development-process.md)

## Contributing

Contributions, testing and feedback are welcome. Read [CONTRIBUTING.md](CONTRIBUTING.md) for the workflow and how to pick issues from the Project, and follow the [Code of Conduct](CODE_OF_CONDUCT.md). Security issues: see [SECURITY.md](SECURITY.md).

## License

[MIT](LICENSE). Rationale in [ADR-0018](docs/adr/0018-license-mit.md).
