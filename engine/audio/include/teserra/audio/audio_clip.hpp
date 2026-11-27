#pragma once
// A sound to play: PCM samples in memory (docs/architecture/audio.md#clips, ADR-0027).

#include <teserra/core/result.hpp>
#include <teserra/core/types.hpp>

#include <memory>
#include <span>
#include <vector>

namespace teserra::audio {

/// The samples of a clip: interleaved `f32` frames (left, right for stereo), at `sampleRate`.
struct ClipSamples {
    u32 sampleRate = 0;
    u32 channels = 0;
    std::vector<f32> samples;
};

/// A sound that voices play (AudioEngine::play()). Copies share the samples, and so do the voices playing
/// it: a clip released while a voice plays it stays in memory until the voice ends. Immutable.
class AudioClip {
public:
    /// The most channels a clip may have: mono or stereo.
    static constexpr u32 kMaxChannels = 2;

    /// An empty clip: it plays nothing, and a voice of it ends at once.
    AudioClip() = default;

    /// A clip of `samples`, interleaved frames of `channels` (1 or 2) at `sampleRate` (8 kHz to 384 kHz).
    /// The mix resamples it to the engine's rate. `samples` must hold whole frames. InvalidArgument
    /// otherwise; an empty `samples` makes an empty clip.
    ///
    ///     std::vector<f32> beep(4800);
    ///     for (usize i = 0; i < beep.size(); ++i) beep[i] = 0.5F * std::sin(f32(i) * 0.06F);
    ///     audio::AudioClip clip = *audio::AudioClip::create(48000, 1, beep);
    [[nodiscard]] static Result<AudioClip> create(u32 sampleRate, u32 channels, std::span<const f32> samples);

    [[nodiscard]] u32 sampleRate() const noexcept { return m_samples ? m_samples->sampleRate : 0; }
    [[nodiscard]] u32 channels() const noexcept { return m_samples ? m_samples->channels : 0; }
    /// Frames: samples per channel.
    [[nodiscard]] u64 frameCount() const noexcept;
    /// Length in seconds at the clip's own sample rate.
    [[nodiscard]] f64 durationSeconds() const noexcept;
    [[nodiscard]] bool isEmpty() const noexcept { return frameCount() == 0; }
    /// The interleaved samples; empty for an empty clip.
    [[nodiscard]] std::span<const f32> samples() const noexcept;

    /// The shared samples, for the voices of AudioEngine; nullptr for an empty clip.
    [[nodiscard]] const std::shared_ptr<const ClipSamples>& shared() const noexcept { return m_samples; }

private:
    explicit AudioClip(std::shared_ptr<const ClipSamples> samples) noexcept
        : m_samples(std::move(samples)) {}

    std::shared_ptr<const ClipSamples> m_samples;
};

} // namespace teserra::audio
