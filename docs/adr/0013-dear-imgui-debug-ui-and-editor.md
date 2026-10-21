# ADR-0013: Dear ImGui for debug UI and the future editor

- **Status:** Accepted
- **Date:** 2026-10-21
- **Related:** [editor and scripting (future)](../architecture/editor-and-scripting-future.md), [ADR-0014](0014-scripting-deferred.md)

## Context

Developers need in-engine debug tools (stats overlay, entity inspector, log console) and eventually an editor. An editor built on unstable engine foundations is rewritten repeatedly. Editor work is more valuable to indie users than scripting, but not urgent for v0.1.

## Decision

- **Dear ImGui** (docking branch, via vcpkg `imgui[docking-experimental]`) is the UI toolkit for debug tools and the editor.
- ImGui is rendered **through the RHI** by an engine-owned ImGui renderer backend (`teserra_renderer` or a `teserra_debug_ui` module). The stock `imgui_impl_opengl3` backend is **not** used, because it would bypass the RHI and leak GL into upper layers. Platform input for ImGui comes from engine `platform::Input` events, not `imgui_impl_glfw`.
- Order: (1) debug overlay and tools in v0.2, (2) editor (separate `editor/` target and executable) from v0.3 onwards, only after the ECS, scene, assets and renderer APIs have been stable for at least two iterations.
- The engine runtime must be usable without ImGui (`TESERRA_WITH_DEBUG_UI=OFF` builds a game with no ImGui code).

## Alternatives considered

| Option | Why not |
|---|---|
| Qt / web-based editor | Large dependency and a second UI paradigm; slower iteration for a solo developer. |
| Custom retained-mode UI | Huge scope; a game UI system is a separate future topic (in-game UI is not ImGui's job). |
| Using `imgui_impl_opengl3` + `imgui_impl_glfw` | Quick, but violates the RHI and platform boundaries. |

## Consequences

**Positive**
- Writing the ImGui backend is a good RHI dogfooding exercise (dynamic buffers, scissors, texture bindings).
- Editor and runtime share one rendering path.

**Negative / costs**
- Custom ImGui backend is ~1 iteration of work (scheduled in v0.2).
- ImGui is not suitable for shipped in-game UI; users need another solution (out of scope for v1).

## Revisit if

- Editor requirements exceed what ImGui handles well (complex asset browsers, node graphs with thousands of nodes).
