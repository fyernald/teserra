# ADR-0006: GLFW behind engine-owned Window and Input interfaces

- **Status:** Accepted
- **Date:** 2026-10-21
- **Related:** [platform abstraction](../architecture/platform-abstraction.md), [ADR-0015](0015-windows-first-platform-abstraction.md)

## Context

We need windows, OpenGL context creation, keyboard/mouse input and later gamepads on Windows first and Linux later (X11 and Wayland). Writing Win32 + X11 + Wayland windowing is a large amount of non-differentiating work. At the same time, GLFW must not become an engine-wide dependency: users and engine code should depend on engine types (`teserra::Key`, `platform::Window`), so GLFW can be replaced (SDL3, native Win32) without touching upper layers.

## Decision

- **GLFW 3.4+** (via vcpkg) implements windowing and raw input on all desktop platforms.
- GLFW is a **`PRIVATE`** dependency of `teserra_platform`. Its headers are included only in `engine/platform/src/glfw/`.
- The engine owns the interfaces: `platform::Platform` (abstract factory: lifetime of the windowing system, event pumping, window creation), `platform::Window`, `platform::OpenGlContext` (only for the GL backend: make current, swap, swap interval, `getProcAddress`), and the backend-independent `platform::Input` state built from engine events.
- GLFW key/button codes are translated to engine enums at the boundary. Engine code never sees GLFW codes, pointers or callbacks.
- Windows expose a `NativeWindowHandle` (tagged struct: Win32 `HWND`/`HINSTANCE`, later X11/Wayland) as opaque pointers, so a future Vulkan backend can create a surface without GLFW.

## Alternatives considered

| Option | Why not |
|---|---|
| SDL3 | Excellent and more complete (audio, gamepads, haptics). Heavier, and its audio/gamepad subsystems overlap with decisions we want to make deliberately later. Remains the most likely replacement if GLFW becomes limiting; the interface makes this a contained swap. |
| Native Win32 | Teaches a lot, but then Linux needs X11 + Wayland code too: large non-differentiating cost for a solo developer. |
| Using GLFW types directly in the engine | Fastest initially, but spreads a third-party dependency across every layer and blocks replacement. |

## Consequences

**Positive**
- Linux support for windows/input is mostly "compile the same GLFW backend on Linux".
- The engine's input model is testable without a window (feed synthetic events into `Input`).

**Negative / costs**
- Event translation boilerplate (key tables). Small and mechanical.
- GLFW's global state (`glfwInit`, event polling on the main thread) must be respected: all window and event calls happen on the main thread, enforced by assertions in debug builds.

## Revisit if

- We need features GLFW lacks (rich gamepad/haptics, IME details, mobile). Evaluate SDL3 behind the same interfaces.
