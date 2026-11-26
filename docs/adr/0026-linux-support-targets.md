# ADR-0026: Linux support targets

- **Status:** Accepted
- **Date:** 2026-11-26
- **Related:** #127 (Linux port, iteration 17), #280–#284, [ADR-0015](0015-windows-first-platform-abstraction.md), [platform abstraction](../architecture/platform-abstraction.md#new-platform-port-checklist), [build and CI](../guides/build-and-ci.md#github-actions)

## Context

v0.2 makes Linux a supported platform ([ADR-0015](0015-windows-first-platform-abstraction.md) kept the code ready for it). "Supported" means CI builds and tests it on every pull request, and bugs on it are bugs, so the set of distributions, compilers and window systems has to be small enough for a one-person project's CI and large enough that most Linux game developers can use it.

What the port found shaped the decision:

- Teserra's API is meant to be written with designated initializers in braced arguments (`device.createGraphicsPipeline({.vertexShader = vs, .targets = {.colorFormats = {format}}})`). GCC 13 and GCC 14.2, the GCCs that Ubuntu 24.04 packages, cannot brace-initialize a struct with a C-array member that has a default member initializer (`GraphicsPipelineDesc::colorBlend`, `RenderPassDesc::colorAttachments`): even `createGraphicsPipeline({})` fails. GCC 14.3 fixed it. Removing the initializer makes GCC 15 and Clang warn about the member in every designated initializer that leaves it out, and `std::array` members would change how games write `.colorAttachments = {{.texture = target}}`.
- GLFW 3.4 runs on X11 and on Wayland. It crashes in `glfwInit()` on a compositor without a seat, which only headless compositors are.
- Mesa's llvmpipe gives every CI runner an OpenGL 4.5 driver, as it does on Windows.

## Decision

- **Distributions:** Ubuntu **24.04 LTS and 26.04 LTS**, x64. Other distributions with the same or newer compilers, Mesa and GLFW's dependencies are best-effort: they usually work, and fixes are welcome, but CI does not run them.
- **Compilers:** **GCC 14.3 or newer, or Clang 18 or newer**, with libstdc++. On Ubuntu 24.04 that means its stock Clang 18; on 26.04, its GCC 15 or Clang 21.
- **Window systems:** **X11 and Wayland**, through GLFW, which picks Wayland when a Wayland session is running. Wayland window decorations come from libdecor where the desktop provides a plugin.
- **GPU:** any OpenGL 4.5 core driver: Mesa (radeonsi, iris, llvmpipe) or a vendor driver.
- **CI:** three blocking jobs, GCC and Clang on 26.04 and Clang on 24.04. Each runs the whole suite on Mesa llvmpipe under Xvfb with GPU tests required, the window tests on Wayland (Weston's X11 backend, which has a seat), and the game template.
- **Release artifacts:** a Linux examples archive next to the Windows zip, from v0.2 (#287).

## Alternatives considered

| Option | Why not (or why not now) |
|---|---|
| Ubuntu, Fedora and Arch in CI | Three package managers and three sets of system packages triple the CI setup and its upkeep for a one-person project. Most Linux game developers use Ubuntu or a derivative; the rest are covered best-effort. |
| Only the newest Ubuntu LTS | Many developers stay on the previous LTS for years; leaving 24.04 out would exclude them until its support ends. |
| Support GCC 13/14.2 by changing the headers | Every option either changes how games write designated initializers (`std::array`) or makes GCC 15 and Clang warn on every pipeline and render pass (no member initializers). Clang 18 on 24.04 costs users one `-DCMAKE_CXX_COMPILER` or one preset. |
| X11 only | Ubuntu and Fedora default to Wayland; XWayland works but scales poorly on HiDPI. GLFW supports both at no extra cost to the engine. |

## Consequences

- `linux-clang-debug` is the preset for Ubuntu 24.04, `linux-gcc-debug` or `linux-clang-debug` for 26.04. The presets name the minimum versions.
- A code change that only GCC 14.3+ or Clang 18+ accepts is fine; one that needs GCC 15 or Clang 21 is not, and CI catches it on 24.04's Clang.
- When Ubuntu 24.04 leaves standard support (2029), the minimums are revisited.
- The game template pins a release tag; until v0.2.0 is released, Linux games set its `GIT_TAG` to `main` (getting-started guide).
