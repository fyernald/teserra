# ADR-0010: stb_image for image decoding

- **Status:** Accepted
- **Date:** 2026-10-21
- **Related:** [assets](../architecture/assets.md), [SECURITY.md](../../SECURITY.md)

## Context

v1 loads PNG (and possibly JPG/TGA) textures for sprites and tilesets. Image decoding is a solved problem; the engine only needs decoded RGBA8 pixels.

## Decision

- **stb_image** (via vcpkg `stb`) decodes images into an engine `ImageData` (width, height, format, pixel bytes) inside `teserra_assets`. It is a `PRIVATE` dependency; exactly one `.cpp` defines `STB_IMAGE_IMPLEMENTATION`.
- Decoding is from memory (`stbi_load_from_memory`), never from paths, so the asset system controls file access.
- Images are decoded to RGBA8 by default; sRGB vs linear is decided by asset metadata/loader options, not by the decoder. Vertical flip on load is **off** (top-left origin, [ADR-0024](0024-coordinate-conventions.md)).
- Decoded image size is validated against a configurable maximum before allocation (defensive against malformed files).

## Alternatives considered

| Option | Why not |
|---|---|
| libpng / libjpeg-turbo | Faster and more robust for huge images, but heavier dependencies for v1 needs. |
| WIC (Windows Imaging Component) | Windows-only. |
| Custom PNG decoder | Out of scope for the learning goals; boring and security-sensitive. |

## Consequences

**Positive**
- Single small dependency, trivial integration.

**Negative / costs**
- stb_image is not hardened against hostile input. v1 treats asset files as trusted developer content; this is documented in SECURITY.md. Runtime loading of untrusted user content (mods) would require a hardened decoder.
- No GPU-compressed formats; a future cooker will produce KTX2/BasisU ([assets](../architecture/assets.md#future)).

## Revisit if

- We ship features that load untrusted images, or profiling shows decode time dominates load times.
