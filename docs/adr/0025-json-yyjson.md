# ADR-0025: yyjson for JSON parsing, behind an engine JSON view

- **Status:** Proposed
- **Date:** 2026-11-24
- **Related:** #90 (Tiled import), #228 (`.asset.json` sidecars), #231 (`.sheet.json` sprite sheets), [assets](../architecture/assets.md), [ADR-0016](0016-error-handling-policy.md), [ADR-0010](0010-stb-image-decoding.md)

## Context

v1 reads several JSON source formats: Tiled maps and tilesets (`.tmj`, `.tsj`), import-settings sidecars (`<file>.asset.json`) and sprite sheet definitions (`.sheet.json`). All of them are read at load time, never per frame, and are developer content, like images ([ADR-0010](0010-stb-image-decoding.md)).

Forces:

- **No exceptions in engine code** ([ADR-0016](0016-error-handling-policy.md)). Parse errors are `Result` errors that name the file and the position, so a designer can fix a hand-edited map.
- **Read-only.** Nothing writes JSON in v1. The editor will, later.
- **Few translation units.** JSON is parsed in a handful of loaders. The library's compile cost must not reach every file that includes an asset header.
- **Small dependency surface:** one vcpkg port, permissive license, Windows and Linux.

## Decision

- We use **yyjson** (vcpkg port `yyjson`, MIT) to parse JSON. It is a `PRIVATE` dependency of `teserra_assets`. No public header includes `yyjson.h`, and the leak check keeps it out.
- `teserra_assets` exposes a small **read-only engine JSON view** in `<teserra/assets/json.hpp>`:
  - `JsonDocument::parse(bytes)` returns `Result<JsonDocument>`. On malformed input it fails with `ErrorCode::InvalidData` and the line, the column and yyjson's message.
  - `JsonValue` is a view into the document: type tests, `asBool/asInt/asNumber/asString` returning `std::optional`, `operator[](key)` for members, and `forEach` / `forEachMember` for iteration.
  - Missing members and wrong types are values that return `std::nullopt`, not errors, so a loader reports them with its own context ("layer 'ground': `width` must be an integer").
- Loaders in other modules (the renderer's `.sheet.json`, sidecars) use that view, so no module other than `teserra_assets` links yyjson.
- Parsing reads the file's bytes in memory, from the asset system's `readFile()`, never from paths.

## Alternatives considered

| Option | Why not (or why not now) |
|---|---|
| nlohmann/json | The most familiar API, but a parse error is either an exception (against ADR-0016, so every parse would need a narrow `try/catch` boundary) or a "discarded" value with no position. It is header-only with heavy templates, so it is costly to compile wherever it is included. |
| simdjson | The fastest parser, and it has an error-code API, but its on-demand API is forward-only, which makes multi-pass reading of a Tiled map awkward. It needs padded input and is a larger dependency than v1 needs. |
| Hand-written parser | Small, but a security- and correctness-sensitive component nobody wants to maintain; error positions and number edge cases are easy to get wrong. |
| A C++ wrapper library around yyjson | An extra dependency for about 200 lines of engine view that we want to control anyway, for the error messages and the `Result` style. |

## Consequences

**Positive**
- Errors with line and column, without exceptions, match the engine's `Result` style.
- One small C library in one module. Its compile cost stays in `json.cpp`.
- yyjson is fast enough that a large Tiled map parses in milliseconds.

**Negative / costs**
- yyjson's C API is lower level. The engine view is code we own and test.
- yyjson is not hardened against hostile input, like stb_image. Asset files are trusted developer content ([SECURITY.md](../../SECURITY.md)).
- `THIRD_PARTY_NOTICES.md` lists yyjson once a package contains it.

**Follow-up work**
- #90: the JSON view and the Tiled map loader.
- #228 and #231 use the view.
- The editor (v0.2+) will need writing; yyjson has a mutable document API, or we revisit.

## Revisit if

- The editor needs to write JSON and the mutable yyjson API proves awkward.
- Profiling shows JSON parsing dominates load times (unlikely; then a binary cooked format is the answer, not another parser).
- yyjson stops being maintained.
