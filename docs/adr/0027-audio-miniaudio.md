# ADR-0027: Audio with miniaudio, decoded or streamed clips

- **Status:** Accepted
- **Date:** 2026-11-27
- **Related:** #126 (audio epic), #310 (the module), #311 (clip files), [audio](../architecture/audio.md), [ADR-0006](0006-glfw-behind-engine-interfaces.md), [ADR-0016](0016-error-handling-policy.md), [ADR-0020](0020-generational-handles.md)

## Context

v0.2 adds sound: effects and music for 2D games on Windows and Linux ([#126](https://github.com/fyernald/teserra/issues/126)). The demo game has been silent since iteration 14, and the survivors-like showcase game (#256) needs audio.

Forces:

- **Small API, hidden library.** As with GLFW ([ADR-0006](0006-glfw-behind-engine-interfaces.md)), games see engine types; the library can be swapped.
- **Testable without a sound card.** CI machines have no audio device, and tests must check what is played, not just that nothing crashes.
- **Windows and Linux** with no new system packages to install: the Linux build already needs X11 and Wayland development packages, and audio should not add more.
- **License:** permissive, nothing to ship as a separate DLL for license reasons.
- **Memory:** music is long. A 3-minute stereo track is ~66 MB as 32-bit floats at 48 kHz, but ~3 MB as Vorbis.
- **No exceptions, no hidden singletons** ([ADR-0016](0016-error-handling-policy.md)).

## Decision

- We use **miniaudio** (vcpkg port `miniaudio`, Unlicense or MIT-0, one C header compiled in one translation unit) as a `PRIVATE` dependency of a new module **`teserra_audio`** (layer 3, depending on core and assets). No public header includes `miniaudio.h`; the leak check keeps `ma_` names inside `engine/audio/src/`.
- **miniaudio's high-level engine** (`ma_engine`, `ma_sound`) mixes, resamples, pans and later spatializes, on miniaudio's own audio thread. Engine calls are main-thread only; voices are `ma_sound`s in a fixed pool of slots, referred to by generational handles ([ADR-0020](0020-generational-handles.md)).
- **Two outputs:** the default sound device, or **offline** (no device; the mix is pulled by `AudioEngine::render()`). Tests use the offline output and check exact samples. A machine without a device runs the game with the offline output and one warning (#312).
- **Clips are decoded into memory by default.** A clip whose sidecar says `{"stream": true}` is **streamed**: it keeps its encoded bytes, and each voice decodes from them while it plays (#311). Streaming reads from memory, because loaders receive bytes, not paths; streaming from disk waits for packed archives.
- **Formats:** WAV through miniaudio's decoder and OGG Vorbis through `stb_vorbis` (from the `stb` port already used for images). FLAC and MP3 are disabled; each is one define and a test away.
- miniaudio's allocations go through `operator new`, so they count like the engine's own, and its log goes to the engine's `Audio` log category.
- **No build option.** miniaudio needs no system libraries at build time: on Linux it opens PulseAudio, PipeWire or ALSA at run time. Audio is part of every engine build.

## Alternatives considered

| Option | Why not (or why not now) |
|---|---|
| OpenAL Soft | LGPL: a DLL to ship and relinking terms. A 3D-positional API from another era; mixing groups and streaming are ours to build. |
| SDL3 audio | Overlaps GLFW, which owns windows and input ([ADR-0006](0006-glfw-behind-engine-interfaces.md)); a second platform library for one subsystem. A low-level stream API: mixing, resampling and groups are ours to build. |
| SoLoud | Built on miniaudio for output, with its own mixer. Less actively maintained, and a layer we do not need. |
| FMOD, Wwise | Proprietary, with license terms and fees for commercial games; overkill for v0.2. A game can still integrate one itself. |
| cubeb, PortAudio | Output only: decoding, mixing and resampling would be ours. |
| miniaudio's low-level device API only | Possible, but mixing, resampling, groups and spatialization are what the high-level engine already does well. |
| Decode everything into memory | Simple, but music would cost tens of MB per track. |
| Choose streaming by file size | Automatic, but surprising: a long sound effect would switch behavior. The sidecar is explicit and per file. |

## Consequences

**Positive**
- One header, no system packages, permissive license; Windows (WASAPI) and Linux (PulseAudio, PipeWire, ALSA) from the same code.
- Deterministic tests on exact samples through the offline output, on every CI machine.
- Mixing, resampling, groups and spatialization come from a well-tested library.

**Negative / costs**
- miniaudio's single header is large (~4 MB): it compiles once, in one translation unit.
- Starting a voice allocates (miniaudio's node for it); a playing voice does not. The steady-state allocation test holds while no voice starts.
- Our types wrap miniaudio's: `AudioEngine`, `AudioClip`, voices, and buses (#312).
- `THIRD_PARTY_NOTICES.md` lists miniaudio, and stb_vorbis with #311.

**Follow-up work**
- #311: clip files, decoded or streamed.
- #312: buses, and the App owns the engine.
- #313: positional sound in the scene.

## Revisit if

- Games need effects (reverb, filters) beyond what miniaudio's node graph gives easily, or a middleware integration.
- Disk streaming becomes necessary (very long tracks, packed archives).
- miniaudio stops being maintained.
