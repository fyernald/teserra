# Minimal game

A starting point for a game on [Teserra](https://github.com/fyernald/teserra): a sprite that moves with WASD or
the arrow keys. Copy this directory to start your own project.

## Prerequisites

The same as for building Teserra ([build and CI guide](https://github.com/fyernald/teserra/blob/main/docs/guides/build-and-ci.md#prerequisites-windows)):

- Visual Studio 2022 17.8+ or Visual Studio 2026 with "Desktop development with C++" (MSVC, CMake, Ninja and vcpkg)
- vcpkg: the copy bundled with Visual Studio (the developer prompts set `VCPKG_ROOT`) or a clone of [vcpkg](https://github.com/microsoft/vcpkg) with `VCPKG_ROOT` set to it
- Git, which FetchContent uses to download Teserra

## Build and run

From a Developer PowerShell for Visual Studio (or any shell where `cl` is on `PATH`):

```sh
cmake --workflow --preset debug
build/debug/minimal_game
```

The first configure downloads Teserra and builds its dependencies with vcpkg, which takes several minutes.
Later builds are incremental. `--preset release` builds an optimized game in `build/release/`.

## What is in it

| File | What it does |
|---|---|
| `CMakeLists.txt` | Fetches Teserra at a release tag, builds `minimal_game` and copies `assets/` next to it |
| `CMakePresets.json` | `debug` and `release` presets; `cmake --workflow` configures and builds in one command |
| `vcpkg.json` | The libraries Teserra needs. vcpkg reads only the top-level project's manifest, so the game lists them |
| `vcpkg-configuration.json` | The vcpkg baseline; keep it equal to Teserra's for the versions Teserra is tested with |
| `src/main.cpp` | The game: an App, a scene with a camera and a sprite, and a system that moves it |
| `assets/hero.png` | The sprite, loaded as `assets://hero.png` |

## Updating Teserra

Change `GIT_TAG` in `CMakeLists.txt` to the new release tag. Compare `vcpkg.json` and
`vcpkg-configuration.json` with the same files in Teserra at that tag and copy over any changes.

To work against a local clone of Teserra instead of the download, configure with
`-DFETCHCONTENT_SOURCE_DIR_TESERRA=<path to the clone>`.
