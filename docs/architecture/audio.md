# Audio

The audio module (`teserra_audio`, layer 3) plays sound: clips mixed into voices by miniaudio, into the sound device or offline ([ADR-0027](../adr/0027-audio-miniaudio.md)). It depends on core and assets; miniaudio is private to it.

Related: [assets](assets.md), [ADR-0020 handles](../adr/0020-generational-handles.md), [memory and performance](memory-and-performance.md).

## Scope

| v0.2 | Future |
|---|---|
| Clips made in code (`AudioClip::create`) | Effects (reverb, filters), recording |
| Voices with volume, pitch, pan and looping | Doppler, cones, occlusion |
| Device or offline output | Choosing a device, hot-plugging |
| WAV and OGG Vorbis files, decoded or streamed (#311) | FLAC and MP3; streaming from disk |
| Volume buses: Master, Music, Effects; the App owns the engine and runs without a device (#312) | User-defined buses |
| `AudioSource` and `AudioListener` components, positional 2D sound (#313) | 3D listeners and cones |

## Clips

```cpp
std::vector<f32> beep(4800);
for (usize i = 0; i < beep.size(); ++i) beep[i] = 0.5F * std::sin(f32(i) * 0.06F);
audio::AudioClip clip = *audio::AudioClip::create(48000, 1, beep);
```

- An `AudioClip` holds interleaved `f32` samples, mono or stereo, at any sample rate from 8 kHz to 384 kHz; the mix resamples it. Bad formats and partial frames are `InvalidArgument`.
- Copies share the samples, and so do the voices that play the clip: a clip released while a voice plays it stays in memory until the voice ends. A clip is immutable.
- An empty clip (`AudioClip{}`) plays nothing.

## Clip files

```cpp
Asset<audio::AudioClip> coin = app.assets().load<audio::AudioClip>("assets://sounds/coin.wav");
Asset<audio::AudioClip> music = app.assets().load<audio::AudioClip>("assets://music/cave.ogg"); // streamed, below
```

- `audio::AudioClipLoader` loads `Asset<AudioClip>` from **`.wav`** (PCM 8, 16, 24 or 32-bit, or float) and **`.ogg`** (Vorbis, through stb_vorbis) files, mono or stereo, at the file's own rate. `audio::registerAssetLoaders(registry)` registers it, with an empty clip as the fallback: a missing sound logs one error and plays nothing. In code, `decodeAudio(bytes)` and `AudioClip::stream(bytes)` do the same from bytes.
- **Decoded** by default: the samples are decoded to `f32` when the clip loads. `AudioDecodeConfig::maxDecodedBytes` (default 256 MiB) refuses longer clips with `Unsupported`, before allocating when the file tells its length.
- **Streamed** when the file's sidecar says `{"stream": true}` (`music/cave.ogg.asset.json`, [import settings](assets.md#import-settings)): the clip keeps the encoded file, and each voice decodes it while it plays. Loaders receive bytes, not paths, so a streamed clip reads from memory, not from disk: a 3-minute Vorbis track takes ~3 MB instead of ~66 MB decoded. Starting a voice of a streamed clip also creates its decoder (an allocation), and several voices of one streamed clip decode independently.
- `isStreamed()` tells the kinds apart. A streamed OGG Vorbis clip's `frameCount()` is 0: the length is unknown until it is decoded.
- **Errors** (logged once with the path by the registry): `InvalidData` for empty and malformed files, cut headers and other formats; `Unsupported` for more than two channels, sample rates outside 8 kHz to 384 kHz, and decoded clips over the limit. A WAV file cut inside its data plays the frames it has: miniaudio clamps the data chunk to the file, as WAV files from streaming tools often give wrong sizes.
- FLAC and MP3 are off ([ADR-0027](../adr/0027-audio-miniaudio.md)); each would be one define and a test.
- **Trust:** like images, sound files are trusted developer content; the decoders are not hardened against hostile files ([SECURITY.md](../../SECURITY.md)).

## Voices

```cpp
auto engine = audio::AudioEngine::create({});   // the default device
audio::VoiceHandle voice = engine->play(clip, {.volume = 0.5F, .pan = -0.3F, .loop = true});
engine->setPitch(voice, 1.5F);
engine->update();                               // once per frame: reclaims voices that ended
engine->stop(voice);
```

- `play(clip, PlayParams)` starts a voice and returns its `VoiceHandle`: `volume` (linear gain), `pitch` (speed and pitch together, greater than 0), `pan` (-1 left to 1 right, balance) and `loop`.
- A voice ends at the end of a clip it does not loop. `isPlaying()` is false from then on; `update()` reclaims the voice, which makes its handle stale. `stop()` ends a voice at once. Setters, `stop()` and `isPlaying()` ignore stale and null handles.
- `AudioEngineConfig::maxVoices` (default 32) voices play at once. When every voice is busy, `play()` returns a null handle and logs a warning once until a voice is free again.
- **Threading:** miniaudio mixes on its own audio thread. Every `AudioEngine` function is for the main thread.
- **Allocation:** starting a voice allocates a little (miniaudio's node for it, and a decoder for a streamed clip); a playing voice does not, and neither do `update()` and the setters. miniaudio allocates through `operator new`.

## Buses

```cpp
engine->play(theme, {.loop = true, .bus = audio::Bus::Music});
engine->setBusVolume(audio::Bus::Music, 0.5F);   // the player's music slider
engine->setBusMuted(audio::Bus::Master, true);   // everything off
```

- A voice plays into a bus (`PlayParams::bus`, default `Effects`): `Music` and `Effects` feed `Master`, which applies to everything, including voices played on `Master` directly.
- `setBusVolume()` (linear, default 1) multiplies with the voices' own volumes and with Master's: a voice at 0.8 on Music at 0.5 with Master at 0.5 plays at 0.2. `setBusMuted()` silences a bus and keeps its volume; its voices go on playing silently.
- The buses are miniaudio sound groups (Master is the engine's own volume). User-defined buses are future work.

## In the App

- The App owns an `AudioEngine` (`app.audio()`), configured by `AppConfig::audio` (`AudioSettings`: `enabled`, `sampleRate`, `channels`, `maxVoices`), and calls its `update()` after each frame. It registers `AudioClipLoader` with its asset registry, so `app.assets().load<audio::AudioClip>(path)` works in every game.
- It opens the default sound device. **Without one** (CI machines, a computer without a sound card, a failed open), it logs one warning, "No sound: ...", and uses the offline output, so the game runs the same, silently. Initialization fails only for invalid settings. `AppConfig::audio.enabled = false`, or `--no-audio` on the command line, uses the offline output without a warning.
- Every scene's world has the engine as the `audio::Audio` resource (`{AudioEngine* engine}`), so systems play sounds: `ctx.resource<audio::Audio>().engine->play(coin)`, with `writesResource<audio::Audio>()`.
- The debug UI's Stats window shows the voices in use and whether a sound device is open.
- miniaudio's own errors go to the log as warnings: a machine without a sound device logs several while miniaudio tries its backends, and the failures that matter come back as results the engine reports.

## Outputs

| Output | Use |
|---|---|
| `Output::Device` | The system's default sound device (WASAPI on Windows; PulseAudio, PipeWire or ALSA on Linux, opened at run time). `create()` fails with `DeviceFailure` without one. The mix is converted to the device's format if it differs. |
| `Output::Offline` | No device. `render(frames)` mixes the next frames on demand, interleaved. Tests check exact samples this way; the App uses it on a machine without sound. |

The mix format is `AudioEngineConfig::sampleRate` (default 48 kHz) and `channels` (1 or 2, default 2). A mono clip plays on both channels.

## Testing

- Offline-output tests play clips of constant values, which every resampler keeps, and check the mixed samples: volumes, panning, looping, ends, stopping, pitch, resampling and released clips (`engine/audio/tests/`).
- Decoding tests write WAV files in code (`engine/audio/tests/wav.hpp`) and decode them to exact samples; `tone.ogg`, an original tone written by `tools/scripts/make_audio_fixtures.py`, decodes close to its sine, and streamed clips mix the same samples as decoded ones.
- A device test opens the default device and skips when there is none (CI machines).
- The leak check keeps miniaudio's names (`ma_*`, `MA_*`) and `<miniaudio.h>` inside `engine/audio/src/`.
