# Getting started

From nothing to your own game on screen: set up the tools, create a project from the template, then grow it into a tiny coin collector while meeting the parts of the engine a game uses every day: the App, scenes, entities and components, systems, input and assets.

Teserra runs on Windows 10 and 11 (x64) and, from v0.2 (on `main` now), on Ubuntu 24.04 and 26.04 LTS ([ADR-0026](../adr/0026-linux-support-targets.md)), with an OpenGL 4.5 driver. The commands below are for Windows; **on Linux**, follow the notes marked *Linux*.

## 1. Install the tools

- **Visual Studio 2022 17.8+ or Visual Studio 2026** with the workload "Desktop development with C++". It brings the compiler, CMake, Ninja and vcpkg. The free Community edition works.
- **Git** ([Git for Windows](https://git-scm.com)). CMake uses it to download Teserra, and vcpkg uses it to download libraries.

Open a **Developer PowerShell for VS** (Start menu, or *Tools > Command Line* in Visual Studio). It puts `cl`, `cmake` and `ninja` on `PATH` and sets `VCPKG_ROOT` to the vcpkg that ships with Visual Studio. Every command below runs there. Check it with:

```powershell
cl
cmake --version     # 3.28 or later
echo $env:VCPKG_ROOT
```

If `VCPKG_ROOT` is empty, install the Visual Studio component "vcpkg package manager", or clone [vcpkg](https://github.com/microsoft/vcpkg), run its `bootstrap-vcpkg.bat`, and set `VCPKG_ROOT` to the clone.

*Linux (Ubuntu):* install the compiler, the build tools and the libraries GLFW needs for X11 and Wayland, then vcpkg:

```bash
sudo apt install build-essential cmake ninja-build git curl zip unzip tar pkg-config \
  libxinerama-dev libxcursor-dev xorg-dev libglu1-mesa-dev \
  libwayland-dev libxkbcommon-dev wayland-protocols extra-cmake-modules
git clone https://github.com/microsoft/vcpkg ~/vcpkg && ~/vcpkg/bootstrap-vcpkg.sh -disableMetrics
echo 'export VCPKG_ROOT=~/vcpkg' >> ~/.bashrc && export VCPKG_ROOT=~/vcpkg
```

Ubuntu 26.04's GCC works as it is. On **Ubuntu 24.04**, use Clang (`sudo apt install clang`, then add `-DCMAKE_CXX_COMPILER=clang++` to the first `cmake --preset` command): its GCCs are too old for Teserra (GCC 14.3 or newer is needed).

## 2. Create a project from the template

The template [`templates/minimal_game`](../../templates/minimal_game/README.md) is a complete game project: a `CMakeLists.txt` that downloads Teserra, presets, the vcpkg manifest, `src/main.cpp` and one sprite. Copy it out of a clone of Teserra:

```powershell
git clone --depth 1 https://github.com/fyernald/teserra.git
Copy-Item -Recurse teserra/templates/minimal_game my_game
cd my_game
cmake --workflow --preset debug
build/debug/minimal_game
```

A window opens with a small hero in the middle; WASD or the arrow keys move it and Escape quits. The first `cmake --workflow` takes several minutes: vcpkg builds the libraries Teserra uses (GLFW, spdlog, glm and others), then Teserra itself is built. Later builds only compile what changed: run `cmake --build build/debug` after each edit.

> **Building against `main` instead of a release?** The template downloads the release named by `GIT_TAG` in its `CMakeLists.txt`. To use a clone of Teserra (for example, before that release exists or to try unreleased changes), configure with `cmake --preset debug -DFETCHCONTENT_SOURCE_DIR_TESERRA=C:/path/to/teserra`, then build with `cmake --build build/debug`.

*Linux:* copy with `cp -r teserra/templates/minimal_game my_game`; the other commands are the same. **Until v0.2.0 is released**, the release the template downloads (v0.1.0) has no Linux support: set `GIT_TAG` in its `CMakeLists.txt` to `main`, or use the `FETCHCONTENT_SOURCE_DIR_TESERRA` line above with the path of your clone.

Make it yours: rename the project in `project(MinimalGame ...)` and the executable in `add_executable(minimal_game ...)` (and in the two lines below it) in `CMakeLists.txt`. The clone of Teserra is no longer needed; you can delete it.

**Something went wrong?**

| Symptom | Fix |
|---|---|
| `VCPKG_ROOT` or a `vcpkg.cmake` error when configuring | Use a Developer PowerShell for VS, or set `VCPKG_ROOT` (step 1) |
| `cl` or `ninja` not found | Same: the developer prompt puts them on `PATH` |
| "Cannot create an OpenGL 4.5 core context" in the log | Update the graphics driver; Teserra needs OpenGL 4.5 |
| A path too long error | Move the project closer to the drive's root, e.g. `C:\dev\my_game` |
| *Linux:* "Cannot initialize GLFW" in the log, no window | Run it from a desktop session (X11 or Wayland), not a plain SSH shell |
| *Linux:* `could not find ... glfw3` or a missing `X11/...` header while vcpkg builds | Install the packages of step 1 |

The log of each run is also written to `teserra.log` next to the executable.

## 3. What is in `main.cpp`

```cpp
engine::App app{{.title = "Minimal game", .windowSize = {1280, 720}, .args = {argc, argv}}};
```

The **App** owns the window, the GPU device, input, the asset registry, the audio engine and the scenes, and runs the main loop. `.args` lets it read the engine's flags, such as `--frames 120` (stop after 120 frames), `--hidden-window`, `--debug-ui` and `--no-audio`. The backtick key (`` ` ``) opens the debug UI, with frame stats and room for windows of your own (`app.debugUi().addWindow()`).

```cpp
scene::Scene& level = app.scenes().create("level");
const ecs::Entity camera = level.spawn();
level.add<scene::Transform>(camera);
level.add<scene::Camera>(camera, scene::Camera{.projection = scene::Orthographic{.height = 10.0F}});
```

A **scene** holds a world of **entities**. An entity is only an ID; what it is comes from its **components**, which are plain structs. A `Transform` places it, a `Camera` makes it look at the world (here 10 world units tall, so 1 unit is 72 pixels in a 720-pixel window), and a `SpriteRenderer` draws a texture at its position, 1 by 1 unit by default. The game adds its own components the same way: `Player` is a struct with a speed.

```cpp
level.systems()
    .add(ecs::Phase::Update, "move_player", movePlayer)
    .reads<Player>()
    .writes<scene::Transform>()
    .readsResource<platform::Input>()
    .readsResource<scene::Time>();
```

**Systems** are functions that run every frame in a phase (`PreUpdate`, `Update`, `PostUpdate`, ...). They see the world through their `SystemContext`: `ctx.view<A, const B>()` loops over the entities that have both components, and `ctx.resource<T>()` reads a world-wide value such as `platform::Input` or `scene::Time`, which the App keeps current. Each system declares what it reads and writes; debug builds stop with an assertion when a system touches something it did not declare, which keeps the scheduler free to order and, later, parallelize them.

```cpp
const int exitCode = app.run(level, {.update = [](engine::App& running, f32) { ... }});
```

`run()` shows `level` and loops until the window closes or `quit()` is called. The `update` callback runs once per frame outside any scene, a good place for app-wide keys such as Escape.

## 4. Grow it: collect coins

Replace `src/main.cpp` with the program below. It adds coins in a ring around the hero; walking into one removes it and counts it.

- **A tag component.** `Coin` has no data; it only marks entities, and a view over `Coin` finds them.
- **A resource.** `Score` is a world resource: one value for the whole scene rather than one per entity.
- **Commands.** A system must not destroy entities while it loops over them; `ctx.commands().destroy()` records the change and applies it at the end of the phase.
- **Ordering.** `.after("move_player")` makes the check see this frame's hero position.
- **Logging.** `TESERRA_LOG_INFO` writes to the console and to `teserra.log`.

Draw a 16x16 `coin.png` and put it in `assets/`, or copy `hero.png` to try it out. A missing file shows a magenta and black checkerboard and logs an error, and the game keeps running.

<!-- tests/getting_started compiles the next block: keep it a complete program. -->
```cpp
#include <teserra/teserra.hpp>

#include <cmath>

using namespace teserra;

struct Player {
    f32 speed = 4.0F; // world units per second
};
struct Coin {}; // a tag: it marks entities and has no data
struct Score {  // a world resource
    u32 collected = 0;
};

namespace {

void movePlayer(ecs::SystemContext& ctx) {
    const auto& input = ctx.resource<const platform::Input>();
    Vec2 direction =
        input.axis2D(platform::Key::A, platform::Key::D, platform::Key::S, platform::Key::W) +
        input.axis2D(platform::Key::Left, platform::Key::Right, platform::Key::Down, platform::Key::Up);
    if (math::length(direction) > 1.0F) {
        direction = math::normalize(direction);
    }
    const f32 dt = ctx.resource<const scene::Time>().deltaSeconds();
    for (auto [entity, transform, player] : ctx.view<scene::Transform, const Player>()) {
        transform.setPosition2D(transform.position2D() + (direction * player.speed * dt));
    }
}

void collectCoins(ecs::SystemContext& ctx) {
    for (const auto [hero, heroTransform, player] : ctx.view<const scene::Transform, const Player>()) {
        for (const auto [coin, coinTransform, tag] : ctx.view<const scene::Transform, const Coin>()) {
            if (math::length(coinTransform.position2D() - heroTransform.position2D()) < 0.6F) {
                ctx.commands().destroy(coin); // applied at the end of the phase
                Score& score = ctx.resource<Score>();
                score.collected += 1;
                TESERRA_LOG_INFO(LogCategory::App, "Coins: {}", score.collected);
            }
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    engine::App app{{.title = "Coin collector", .windowSize = {1280, 720}, .args = {argc, argv}}};
    if (!app.isInitialized()) {
        return 1;
    }

    scene::Scene& level = app.scenes().create("level");
    level.world().insertResource<Score>();

    // Textures stay loaded while their Asset handles live: here, until main() returns.
    const auto heroTexture = app.assets().load<renderer::Texture>("assets://hero.png");
    const auto coinTexture = app.assets().load<renderer::Texture>("assets://coin.png");

    const ecs::Entity camera = level.spawn();
    level.add<scene::Transform>(camera);
    level.add<scene::Camera>(camera,
                             scene::Camera{.projection = scene::Orthographic{.height = 10.0F},
                                           .clearColor = {.r = 0.1F, .g = 0.12F, .b = 0.16F, .a = 1.0F}});

    const ecs::Entity hero = level.spawn();
    level.add<scene::Transform>(hero);
    // Higher layers draw on top: the hero walks over the coins.
    level.add<scene::SpriteRenderer>(
        hero, scene::SpriteRenderer{.texture = heroTexture.get().handle(), .layer = 1});
    level.add<Player>(hero);

    constexpr int kCoins = 12;
    for (int i = 0; i < kCoins; ++i) {
        const f32 angle = static_cast<f32>(i) * 6.2831853F / static_cast<f32>(kCoins);
        const ecs::Entity coin = level.spawn();
        level.add<scene::Transform>(
            coin, scene::Transform{.position = {std::cos(angle) * 3.5F, std::sin(angle) * 3.5F, 0.0F}});
        level.add<scene::SpriteRenderer>(
            coin, scene::SpriteRenderer{.texture = coinTexture.get().handle(), .size = Vec2{0.6F}});
        level.add<Coin>(coin);
    }

    level.systems()
        .add(ecs::Phase::Update, "move_player", movePlayer)
        .reads<Player>()
        .writes<scene::Transform>()
        .readsResource<platform::Input>()
        .readsResource<scene::Time>();
    level.systems()
        .add(ecs::Phase::Update, "collect_coins", collectCoins)
        .reads<scene::Transform>()
        .reads<Player>()
        .reads<Coin>()
        .writesResource<Score>()
        .after("move_player");

    const int exitCode = app.run(level, {.update = [](engine::App& running, f32) {
                                     if (running.input().wasPressed(platform::Key::Escape)) {
                                         running.quit();
                                     }
                                 }});
    log::flush();
    return exitCode;
}
```

Build and run it again:

```powershell
cmake --build build/debug
build/debug/minimal_game
```

## 5. Input

`platform::Input` is a resource that the App refreshes from the window's events at the start of every frame:

| Call | True or the value |
|---|---|
| `isDown(Key::Space)` | while the key is held |
| `wasPressed(Key::Space)`, `wasReleased(...)` | in the one frame it went down or up |
| `axis2D(left, right, down, up)` | a `Vec2` from four keys, each axis -1, 0 or 1 |
| `isDown(MouseButton::Left)`, `mousePosition()`, `scrollDelta()` | the mouse buttons, the cursor in window coordinates, the wheel |
| `text()` | the text typed this frame, UTF-8 |

Keys are physical positions named after the US layout, so `Key::W` is the key above `S` on any keyboard. Systems read input with `.readsResource<platform::Input>()`; App callbacks use `app.input()`.

## 6. Assets

`app.assets().load<renderer::Texture>("assets://coin.png")` reads a PNG through the mount `assets://`, which points at the `assets` folder next to the executable. The template's `teserra_copy_assets(minimal_game)` copies your project's `assets/` folder there on every build, and only the files that changed, so an edited image needs only `cmake --build`.

- Loading the same path again returns the same texture. It is unloaded when the last `Asset` handle to it goes away, so keep the handle alive as long as sprites show it.
- A file that is missing or broken gives the fallback texture and an error in the log, never a crash.
- Many small images belong in one sprite sheet: `renderer::SpriteSheet::grid(texture, {.cellSize = {16, 16}, .count = 3})` cuts one texture into cells (it returns a `Result`, an error if the cells do not fit), and `scene::spriteFromSheet(sheet, 2)` makes a `SpriteRenderer` for one cell. `examples/scene_sprites` uses it.
- For crisp pixel art, set `.spritePass = {.filter = rhi::Filter::Nearest}` in the App's config, and render at a low resolution scaled by whole factors with `.sceneTarget = {.resolution = {320, 180}}` (`examples/pixel_perfect`).

## 7. Where to go next

- [The examples](../../examples/README.md): what each example shows and which parts of the engine it uses. `examples/demo_game` is a complete small game with menus, collisions, a tilemap and a HUD.
- [Public API](../architecture/public-api.md): the principles behind the API and a slightly bigger game.
- [Architecture overview](../architecture/overview.md), [ECS](../architecture/ecs.md), [assets](../architecture/assets.md) and [renderer](../architecture/renderer.md): how the parts work inside.
- Questions, bugs and ideas: [issues](https://github.com/fyernald/teserra/issues/new/choose).

## Shipping your game

A release build: `cmake --workflow --preset release` builds `build/release/minimal_game.exe`. Ship the executable together with:

- the DLLs next to it in `build/release` (GLFW, spdlog, fmt, yyjson);
- the `assets` folder next to it;
- the Visual C++ runtime: either the `msvcp140.dll`, `vcruntime140.dll` and `vcruntime140_1.dll` from Visual Studio's redistributable folder (`%VCToolsRedistDir%\x64\Microsoft.VC*.CRT`, set in the developer prompt) next to the executable, or ask players to install the Visual C++ Redistributable;
- the licenses: Teserra is MIT-licensed, which asks you to include its copyright and license notice ([LICENSE](../../LICENSE)), and the libraries it uses have notices of their own ([THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md)). Putting both files next to the executable covers it.
