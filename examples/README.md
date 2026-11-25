# Examples

Each folder is one runnable program, built with the engine when Teserra is the top-level project (`TESERRA_BUILD_EXAMPLES`, on by default). After `cmake --workflow --preset windows-msvc-debug`, an example is at `build/windows-msvc-debug/examples/<name>/teserra_<name>.exe`. Every program with a window quits on Escape, and runs as a CTest smoke test with `--frames N --hidden-window` ([testing strategy](../docs/guides/testing-strategy.md#example-smoke-runs)).

To start a game of your own, use the [game template](../templates/minimal_game/README.md) and the [getting-started guide](../docs/guides/getting-started.md) instead: the examples live inside the engine's build.

## Games and scenes

The public API a game uses: the App, scenes, components, systems and assets.

| Example | What it shows | Engine parts |
|---|---|---|
| [`demo_game`](demo_game/main.cpp) | A small top-down game: collect the coins, avoid spikes and slimes, reach the door. Menu, level and win screen are scenes; Enter starts, F1 shows the colliders | Scenes and switching, components and systems, collisions with events, tilemap, render target at 320x180, art and font drawn in code. Uses only the public API (checked at configure time) |
| [`pixel_perfect`](pixel_perfect/main.cpp) | A pixel-art scene at 320x180, scaled by whole factors and letterboxed. A camera parented to the hero, a minimap from a second camera. F1 debug draw, F2 native resolution | Scene render target, nearest filtering, hierarchy, render-to-texture, debug draw, `Texture::create()` |
| [`tilemap`](tilemap/main.cpp) | A 256x256 map with three layers that scrolls and zooms; Space plants flowers, which rebuilds one chunk. F1 shows the chunks | Tilemaps and tilesets, chunk meshes and culling, debug draw, a tileset PNG in `assets/` |
| [`scene_sprites`](scene_sprites/main.cpp) | A hero moving over grass with bobbing coins; the camera follows | Scenes, entities, components, systems with declared access and ordering, a sprite sheet in `assets/` |

## Renderer and RHI

Lower layers, for engine work and for checking hardware.

| Example | What it shows | Engine parts |
|---|---|---|
| [`sprite_stress`](sprite_stress/main.cpp) | Thousands of moving sprites with 16 textures; 1 and 2 switch between 1000 and 10 000; pan, zoom, FPS in the title. Vsync off | Renderer sprite pass and batching, 2D camera, `--sprites N` |
| [`textured_quad`](textured_quad/main.cpp) | A PNG on a quad; Space switches nearest and linear filtering | RHI textures, samplers and bind groups, image decoding, embedded shaders |
| [`rhi_triangle`](rhi_triangle/main.cpp) | A turning, vertex-colored triangle | RHI buffers, shader modules, graphics pipeline, per-frame uniform buffer, `teserra_add_shaders()` |
| [`rhi_clear`](rhi_clear/main.cpp) | A window whose color cycles | The smallest RHI frame: one render pass that clears, then present |
| [`window_input`](window_input/main.cpp) | An empty window; keys, buttons and typed text go to the log, FPS and a WASD-moved position to the title | Platform: window, events, `Input`, frame timing, without the renderer |

## Console programs

| Example | What it shows | Engine parts |
|---|---|---|
| [`hello`](hello/main.cpp) | Prints the engine version | The smallest program that links Teserra |
| [`core_sandbox`](core_sandbox/main.cpp) | Logging in several categories, a recoverable error, handles, math; `--assert` fails an assertion | Core: log, `Result`, `Handle`/`HandlePool`, math, assertions |
| [`ecs_sandbox`](ecs_sandbox/main.cpp) | 100 000 entities moved through views, with a timing for each step; `--entities N` | ECS storage and views. Build Release for meaningful numbers |

## Release package

The windowed examples above are also in each release's `teserra-<version>-windows-x64-examples.zip`, ready to run without building ([release process](../docs/process/release-process.md#release-artifacts)).
