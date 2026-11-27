#pragma once
// A sound to play: samples decoded into memory, or an encoded file streamed while it plays
// (docs/architecture/audio.md#clips, ADR-0027).

#include <teserra/assets/asset_registry.hpp>
#include <teserra/core/result.hpp>
#include <teserra/core/types.hpp>

#include <cstddef>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace teserra::audio {

/// The data of a clip, shared by the clip's copies and the voices playing it.
struct ClipData {
    u32 sampleRate = 0;
    u32 channels = 0;
    /// Samples per channel; 0 for a streamed clip whose decoder cannot tell before decoding (OGG Vorbis).
    u64 frameCount = 0;
    /// A decoded clip's interleaved samples (left, right for stereo).
    std::vector<f32> samples;
    /// A streamed clip's file, decoded by each voice while it plays.
    std::vector<std::byte> encoded;
};

/// Limits of decodeAudio(), checked before the samples are allocated where the file tells its length.
struct AudioDecodeConfig {
    /// The most bytes of decoded `f32` samples a clip may need. A longer file is Unsupported: stream it.
    usize maxDecodedBytes = usize{256} * 1024 * 1024;
};

/// A sound that voices play (AudioEngine::play()): samples in memory, or a file streamed while it plays.
/// Copies share the data, and so do the voices playing it: a clip released while a voice plays it stays in
/// memory until the voice ends. Immutable.
class AudioClip {
public:
    /// The most channels a clip may have: mono or stereo.
    static constexpr u32 kMaxChannels = 2;

    /// An empty clip: it plays nothing. It is the fallback of clips that fail to load.
    AudioClip() = default;

    /// A clip of `samples`, interleaved frames of `channels` (1 or 2) at `sampleRate` (8 kHz to 384 kHz).
    /// The mix resamples it to the engine's rate. `samples` must hold whole frames. InvalidArgument
    /// otherwise; an empty `samples` makes an empty clip.
    ///
    ///     std::vector<f32> beep(4800);
    ///     for (usize i = 0; i < beep.size(); ++i) beep[i] = 0.5F * std::sin(f32(i) * 0.06F);
    ///     audio::AudioClip clip = *audio::AudioClip::create(48000, 1, beep);
    [[nodiscard]] static Result<AudioClip> create(u32 sampleRate, u32 channels, std::span<const f32> samples);

    /// A streamed clip of a WAV or OGG Vorbis file in memory: it keeps a copy of `file`, and each voice
    /// decodes it while it plays. The file's header is checked now; errors as decodeAudio().
    [[nodiscard]] static Result<AudioClip> stream(std::span<const std::byte> file);

    [[nodiscard]] u32 sampleRate() const noexcept { return m_data ? m_data->sampleRate : 0; }
    [[nodiscard]] u32 channels() const noexcept { return m_data ? m_data->channels : 0; }
    /// Frames: samples per channel. 0 for an empty clip, and for a streamed OGG Vorbis clip (unknown).
    [[nodiscard]] u64 frameCount() const noexcept { return m_data ? m_data->frameCount : 0; }
    /// Length in seconds; 0 when frameCount() is.
    [[nodiscard]] f64 durationSeconds() const noexcept;
    [[nodiscard]] bool isEmpty() const noexcept { return m_data == nullptr; }
    [[nodiscard]] bool isStreamed() const noexcept { return m_data != nullptr && !m_data->encoded.empty(); }
    /// A decoded clip's interleaved samples; empty for empty and streamed clips.
    [[nodiscard]] std::span<const f32> samples() const noexcept;

    /// The shared data, for the voices of AudioEngine; nullptr for an empty clip.
    [[nodiscard]] const std::shared_ptr<const ClipData>& data() const noexcept { return m_data; }

private:
    explicit AudioClip(std::shared_ptr<const ClipData> data) noexcept
        : m_data(std::move(data)) {}

    std::shared_ptr<const ClipData> m_data;
};

/// Decodes a WAV (PCM 8, 16, 24 or 32-bit, or float) or OGG Vorbis file in memory into a clip of `f32`
/// samples at the file's own rate. Errors: InvalidData for empty and malformed files, cut headers, other
/// formats and files that end before the length their header gives (except WAV files: their data is clamped
/// to the file, which plays the frames present); Unsupported for more than two channels and for clips over
/// config.maxDecodedBytes.
[[nodiscard]] Result<AudioClip> decodeAudio(std::span<const std::byte> file,
                                            const AudioDecodeConfig& config = {});

/// Loads Asset<AudioClip> from .wav and .ogg files: decoded by default, streamed when the file's sidecar says
/// {"stream": true} (assets::AssetImportSettings::stream).
class AudioClipLoader final : public assets::AssetLoader<AudioClip> {
public:
    explicit AudioClipLoader(const AudioDecodeConfig& config = {}) noexcept
        : m_config(config) {}

    [[nodiscard]] std::string_view typeName() const override { return "AudioClip"; }
    /// {"wav", "ogg"}
    [[nodiscard]] std::span<const std::string_view> extensions() const override;
    [[nodiscard]] Result<AudioClip> load(assets::AssetLoadContext& context,
                                         std::span<const std::byte> bytes) override;

private:
    AudioDecodeConfig m_config;
};

/// Registers AudioClipLoader with `registry`, and an empty clip as the fallback of clips that fail to load,
/// so a missing sound logs one error and plays nothing.
void registerAssetLoaders(assets::AssetRegistry& registry);

} // namespace teserra::audio
