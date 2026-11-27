#pragma once
// miniaudio as Teserra builds it (ADR-0027). Every file of the module includes miniaudio through this header,
// so the translation unit that compiles its implementation (miniaudio.cpp) and those that use it agree on
// the features:
// - no resource manager: clips come from Teserra's asset registry, not from miniaudio's file loading;
// - no encoding, waveform generation, FLAC or MP3: Teserra plays WAV and OGG Vorbis (#311), and FLAC or MP3
//   would each be one define less, plus a test;
// - OGG Vorbis through stb_vorbis: miniaudio decodes Vorbis when stb_vorbis's declarations come before it.
//   miniaudio.cpp compiles stb_vorbis's implementation after miniaudio's.

// The libraries' configuration macros have their names.
// NOLINTBEGIN(readability-identifier-naming)
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_FLAC
#define MA_NO_MP3

// The order matters (stb_vorbis first), so clang-format must not sort these includes.
// clang-format off
#define STB_VORBIS_HEADER_ONLY
// NOLINTEND(readability-identifier-naming)
#include <stb_vorbis.c> // NOLINT(bugprone-suspicious-include): stb_vorbis is one .c file, included as its header
#include <miniaudio.h>
// clang-format on

namespace teserra::audio::detail {

/// miniaudio's allocations through operator new, so they count like the engine's own (tests count them).
[[nodiscard]] ma_allocation_callbacks allocationCallbacks() noexcept;

} // namespace teserra::audio::detail
