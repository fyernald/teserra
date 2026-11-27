# ADR-0028: Public API conventions from the v0.2 review

- **Status:** Proposed
- **Date:** 2026-11-27
- **Related:** #264 (the findings), #119 (the v0.1 review), [public API](../architecture/public-api.md), [ADR-0020 handles](0020-generational-handles.md), [ADR-0021 naming](0021-naming-conventions.md)

## Context

The public API review before v0.1 (#119) left five findings that needed a decision rather than a quick fix (#264). Before 1.0, minor versions may break the API ([release process](../process/release-process.md#versioning)); deciding now lets v0.3 make the breaking changes early, in one release, with migration notes, before the showcase game (#256) and the editor build on the API.

Iteration 19 already took one side of finding 1: `scene::AudioSource` holds an `assets::Asset<audio::AudioClip>` (#313).

## Decision

1. **Components hold assets as `assets::Asset<T>`.** A component that shows or plays an asset keeps it loaded: `SpriteRenderer::texture` becomes `Asset<renderer::Texture>`, `TilemapRenderer::tileset` an `Asset<scene::Tileset>`, and `SpriteRenderer::material` an `Asset<renderer::MaterialInstance>`; tilesets and material instances made in code go through `load<T>(path, create)` like textures do. Copying a component copies a non-atomic reference count; extraction reads `asset.get()`, an index and a generation check per sprite. **Condition:** `sprite_stress` at 10 000 sprites (Release) may lose at most 5 % of its frame rate, and the steady-state frame stays allocation-free; otherwise the component also caches the resolved `rhi::TextureHandle` when the asset is set.
2. **Backend-facing headers move to `teserra/rhi/backend/`**, namespace `teserra::rhi::backend`, documented as the backend API: what backends (and tests) implement against, not what games use. `<teserra/rhi/commands.hpp>` (the command records) and `<teserra/rhi/validation.hpp>` (stored descriptors) go there. `command_list.hpp` keeps including the records, so they stay reachable, but games and examples include nothing from `backend/`, and the public-include check (`teserra_check_public_includes()`) refuses it for the demo game and the examples.
3. **`App::renderer()` returns a reference**, like `device()` and `audio()`. The App creates its renderer during initialization when it has a device; a failure fails initialization (`initError()`), instead of a null pointer at first use. `renderer()` asserts that initialization succeeded and that there is a device.
4. **Descriptors are top-level `XxxDesc` types**, as the 14 of the RHI and the renderer are: `renderer::Material::Desc` becomes `renderer::MaterialDesc`. Settings of a service stay `XxxConfig` (`AppConfig`, `AudioEngineConfig`): a `Desc` describes an object to create, a `Config` how a service runs.
5. **A `teserra::prelude` namespace** in `<teserra/teserra.hpp>`: using-declarations for the types a small game names most (`App`, `AppConfig`, `Scene`, `Transform`, `SpriteRenderer`, `Camera`, `SystemContext`, `Phase`, `Key`, `Time`, `Asset`, `AudioClip`, ...). `using namespace teserra::prelude;` shortens game code; engine code and the examples that show the layers keep qualified names. The prelude only adds names; it never renames.

## Alternatives considered

| Option | Why not |
|---|---|
| Keep raw handles in components, document the rule | The rule is easy to break (a texture unloaded under a sprite) and the failure is a GPU-side use of a dead handle, hard to debug. |
| `std::shared_ptr` instead of `Asset<T>` | Atomic counts and a second ownership system next to the registry, which already counts and unloads at the end of the frame. |
| Hide the backend headers entirely (pimpl `CommandList`) | `CommandList` records inline into those structs on purpose (allocation-free recording, ADR-0005); a pimpl would cost an indirection per command. |
| `App::renderer()` returning `Result<Renderer&>` | Every call site would unwrap it, for a failure that only happens at startup. |
| Nested `Desc` types everywhere | 14 types would move instead of one; top-level names read better in signatures. |
| No prelude, qualified names always | Explicit, but the dream-code example is noticeably longer than it needs to be for a small game. |

## Consequences

**Positive**
- Asset lifetimes are automatic in every component; the rule "keep the asset alive" disappears from the docs.
- The public surface games see is smaller and named for what it is.
- One convention for descriptors and for service access.

**Negative / costs**
- Breaking changes in v0.3 for every game: the changelog lists each with a migration line (**BREAKING:**).
- Finding 1 needs a measurement before it lands, and may add a cached handle.

**Follow-up work (v0.3 issues, created at v0.3's planning)**
- Components hold `Asset<T>`, with the `sprite_stress` measurement (finding 1).
- `teserra/rhi/backend/` and the public-include check (finding 2).
- `App::renderer()` as a reference, created at initialization (finding 3).
- `renderer::MaterialDesc` (finding 4).
- `teserra::prelude` and the dream-code example rewritten with it (finding 5).

## Revisit if

- The measurement of finding 1 shows a cost the cached handle cannot remove.
- A second backend (Vulkan) needs more of the backend API than `backend/` holds.
