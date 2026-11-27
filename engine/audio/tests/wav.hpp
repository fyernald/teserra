#pragma once
// Test helper: writes WAV files in memory, PCM 16-bit or 32-bit float, so tests can choose their samples,
// cut them and corrupt them exactly.

#include <teserra/core/types.hpp>

#include <cstddef>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

namespace teserra::test {

enum class WavFormat : u8 {
    Pcm16,
    Float32
};

namespace detail {

inline void put(std::vector<std::byte>& out, const void* data, usize size) {
    const auto* bytes = static_cast<const std::byte*>(data);
    out.insert(out.end(), bytes, bytes + size);
}

inline void putTag(std::vector<std::byte>& out, std::string_view tag) {
    put(out, tag.data(), tag.size());
}

template <typename T>
void putLittle(std::vector<std::byte>& out, T value) {
    // Every platform Teserra supports is little-endian, like WAV.
    put(out, &value, sizeof(value));
}

} // namespace detail

/// A WAV file of `samples` (interleaved, -1 to 1) with `channels` at `sampleRate`.
inline std::vector<std::byte> writeWav(WavFormat format, u32 sampleRate, u16 channels,
                                       std::span<const f32> samples) {
    const u16 bytesPerSample = format == WavFormat::Pcm16 ? 2 : 4;
    const auto dataSize = static_cast<u32>(samples.size() * bytesPerSample);
    std::vector<std::byte> out;
    detail::putTag(out, "RIFF");
    detail::putLittle<u32>(out, 4 + (8 + 16) + (8 + dataSize));
    detail::putTag(out, "WAVE");
    detail::putTag(out, "fmt ");
    detail::putLittle<u32>(out, 16);
    detail::putLittle<u16>(out, format == WavFormat::Pcm16 ? 1 : 3); // PCM or IEEE float
    detail::putLittle<u16>(out, channels);
    detail::putLittle<u32>(out, sampleRate);
    detail::putLittle<u32>(out, sampleRate * channels * bytesPerSample);
    detail::putLittle<u16>(out, static_cast<u16>(channels * bytesPerSample));
    detail::putLittle<u16>(out, static_cast<u16>(bytesPerSample * 8));
    detail::putTag(out, "data");
    detail::putLittle<u32>(out, dataSize);
    for (const f32 sample : samples) {
        if (format == WavFormat::Pcm16) {
            detail::putLittle<i16>(out, static_cast<i16>(sample * 32768.0F));
        } else {
            detail::putLittle<f32>(out, sample);
        }
    }
    return out;
}

} // namespace teserra::test
