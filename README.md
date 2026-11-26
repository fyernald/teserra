# Teserra

**A modern C++20 game engine for indie developers.** 2D first, built on an architecture that is ready for 3D, Vulkan and Linux from day one.

[![CI](https://github.com/fyernald/teserra/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/fyernald/teserra/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Release: v0.1.0](https://img.shields.io/badge/release-v0.1.0-blue.svg)](https://github.com/fyernald/teserra/releases/tag/v0.1.0)
![Status: alpha](https://img.shields.io/badge/status-alpha-orange.svg)

> **Status: [v0.1.0](https://github.com/fyernald/teserra/releases/tag/v0.1.0) is out, the first public release.** The 2D engine works on Windows: sprites, tilemaps, scenes with an ECS, assets, input and a pixel-perfect render path, shown by a [small demo game](examples/demo_game/main.cpp). Try the examples from the release's zip, or [make a game](docs/guides/getting-started.md) with it. The API can still change before 1.0. Follow progress on the **[GitHub Project](https://github.com/users/fyernald/projects/3)**.

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

### Supported platforms

| | Windows (v0.1) | Linux (v0.2, on `main` now) |
|---|---|---|
| Operating system | Windows 10 and 11, x64 | Ubuntu 24.04 and 26.04 LTS, x64, on X11 or Wayland; other distributions best-effort |
| GPU | Any GPU with an OpenGL 4.5 core driver | Any GPU with an OpenGL 4.5 core driver (Mesa or vendor) |
| Compiler | MSVC from Visual Studio 2022 17.8+ or 2026 | GCC 14.3+ or Clang 18+ (on Ubuntu 24.04, Clang) |

Linux support is decided in [ADR-0026](docs/adr/0026-linux-support-targets.md); CI builds and tests every supported target on each pull request.

The detailed roadmap, iterations and backlog live **only** in the [GitHub Project](https://github.com/users/fyernald/projects/3) and [issues](https://github.com/fyernald/teserra/issues).

## Quickstart

### Make a game

Copy the game template and build it, from a **Developer PowerShell for VS** (Visual Studio 2022 17.8+ or 2026 with "Desktop development with C++", and git):

```powershell
git clone --depth 1 https://github.com/fyernald/teserra.git
Copy-Item -Recurse teserra/templates/minimal_game my_game
cd my_game
cmake --workflow --preset debug
build/debug/minimal_game
```

On Linux the same steps work from a terminal once the packages are installed ([getting started](docs/guides/getting-started.md#1-install-the-tools)); until v0.2.0 is released, point the template's `GIT_TAG` at `main`.

The template fetches Teserra with CMake's FetchContent, so the game is its own project. The **[getting-started guide](docs/guides/getting-started.md)** walks through it and grows it into a small game; the [examples](examples/README.md) show the rest of the engine.

A game in Teserra looks like this:

```cpp
#include <teserra/teserra.hpp>

using namespace teserra;

struct Player {
    f32 speed = 4.0F;
};

void movePlayer(ecs::SystemContext& ctx) {
    const auto& input = ctx.resource<const platform::Input>();
    const Vec2 direction =
        input.axis2D(platform::Key::A, platform::Key::D, platform::Key::S, platform::Key::W);
    const f32 dt = ctx.resource<const scene::Time>().deltaSeconds();
    for (auto [entity, transform, player] : ctx.view<scene::Transform, const Player>()) {
        transform.setPosition2D(transform.position2D() + (direction * player.speed * dt));
    }
}

int main(int argc, char** argv) {
    engine::App app{{.title = "My game", .windowSize = {1280, 720}, .args = {argc, argv}}};
    scene::Scene& level = app.scenes().create("level");
    const auto heroTexture = app.assets().load<renderer::Texture>("assets://hero.png");

    const ecs::Entity camera = level.spawn();
    level.add<scene::Transform>(camera);
    level.add<scene::Camera>(camera, scene::Camera{.projection = scene::Orthographic{.height = 10.0F}});

    const ecs::Entity hero = level.spawn();
    level.add<scene::Transform>(hero);
    level.add<scene::SpriteRenderer>(hero, scene::SpriteRenderer{.texture = heroTexture.get().handle()});
    level.add<Player>(hero);

    level.systems()
        .add(ecs::Phase::Update, "move_player", movePlayer)
        .reads<Player>()
        .writes<scene::Transform>()
        .readsResource<platform::Input>()
        .readsResource<scene::Time>();
    return app.run(level);
}
```

Design and rationale: [public API](docs/architecture/public-api.md).

### Work on the engine

**New machine?** Install [Git for Windows](https://git-scm.com), then in Git Bash:

```bash
git clone https://github.com/fyernald/teserra.git
cd teserra
bash tools/setup/setup.sh
```

It checks what the build needs (Visual Studio Build Tools with the C++ workload, CMake, Ninja, vcpkg, the Windows SDK, Python, clang-format / clang-tidy), downloads and installs what is missing with a progress bar, explains any problem with a fix, then configures, builds and runs the tests. `--check` only reports; `--help` lists the options.

**By hand:** Requirements: Windows 10/11 x64, Visual Studio 2022 17.8+ or 2026 with "Desktop development with C++" (or MSVC Build Tools + CMake 3.28 + Ninja + vcpkg), and git. From a **Developer PowerShell for VS**:

```bash
git clone https://github.com/fyernald/teserra.git
cd teserra
cmake --workflow --preset windows-msvc-debug
build/windows-msvc-debug/examples/demo_game/teserra_demo_game
```

The workflow preset configures, builds and runs the tests; the first run also builds the dependencies through vcpkg, which takes a few minutes. Every example is in `build/windows-msvc-debug/examples/<name>/`; [examples/README.md](examples/README.md) says what each one shows. Details, other presets and IDE setup: [build and CI](docs/guides/build-and-ci.md).

## Documentation

- [Getting started](docs/guides/getting-started.md) · [Examples](examples/README.md)
- [Documentation index](docs/README.md)
- [Architecture overview](docs/architecture/overview.md) · [RHI](docs/architecture/rhi.md) · [Renderer](docs/architecture/renderer.md) · [ECS](docs/architecture/ecs.md)
- [Architecture Decision Records](docs/adr/README.md)
- [Development process](docs/process/development-process.md)

## Contributing

Contributions, testing and feedback are welcome. Read [CONTRIBUTING.md](CONTRIBUTING.md) for the workflow and how to pick issues from the Project, and follow the [Code of Conduct](CODE_OF_CONDUCT.md). Security issues: see [SECURITY.md](SECURITY.md).

## License

[MIT](LICENSE). Rationale in [ADR-0018](docs/adr/0018-license-mit.md).
