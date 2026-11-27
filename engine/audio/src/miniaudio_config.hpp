#pragma once
// miniaudio as Teserra builds it (ADR-0027). Every file of the module includes miniaudio through this header,
// so the translation unit that compiles its implementation (miniaudio.cpp) and those that use it agree on
// the features:
// - no resource manager: clips come from Teserra's asset registry, not from miniaudio's file loading;
// - no encoding, waveform generation, FLAC or MP3: Teserra plays WAV and OGG Vorbis (#311), and FLAC or MP3
//   would each be one define less, plus a test.

#define MA_NO_RESOURCE_MANAGER
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_FLAC
#define MA_NO_MP3

#include <miniaudio.h>

namespace teserra::audio::detail {

/// miniaudio's allocations through operator new, so they count like the engine's own (tests count them).
[[nodiscard]] ma_allocation_callbacks allocationCallbacks() noexcept;

} // namespace teserra::audio::detail
