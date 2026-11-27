#include <teserra/audio/audio_clip.hpp>

#include <utility>

namespace teserra::audio {

namespace {

constexpr u32 kMinSampleRate = 8'000;
constexpr u32 kMaxSampleRate = 384'000;

} // namespace

Result<AudioClip> AudioClip::create(u32 sampleRate, u32 channels, std::span<const f32> samples) {
    if (channels == 0 || channels > kMaxChannels) {
        return fail(ErrorCode::InvalidArgument, Module::Audio, "an audio clip has 1 or 2 channels, not {}",
                    channels);
    }
    if (sampleRate < kMinSampleRate || sampleRate > kMaxSampleRate) {
        return fail(ErrorCode::InvalidArgument, Module::Audio,
                    "an audio clip's sample rate is {} Hz to {} Hz, not {} Hz", kMinSampleRate,
                    kMaxSampleRate, sampleRate);
    }
    if (samples.size() % channels != 0) {
        return fail(ErrorCode::InvalidArgument, Module::Audio,
                    "{} samples are not whole frames of {} channels", samples.size(), channels);
    }
    if (samples.empty()) {
        return AudioClip{};
    }
    return AudioClip{std::make_shared<const ClipSamples>(ClipSamples{
        .sampleRate = sampleRate, .channels = channels, .samples = {samples.begin(), samples.end()}})};
}

u64 AudioClip::frameCount() const noexcept {
    return m_samples ? m_samples->samples.size() / m_samples->channels : 0;
}

f64 AudioClip::durationSeconds() const noexcept {
    return m_samples ? static_cast<f64>(frameCount()) / static_cast<f64>(m_samples->sampleRate) : 0.0;
}

std::span<const f32> AudioClip::samples() const noexcept {
    return m_samples ? std::span<const f32>{m_samples->samples} : std::span<const f32>{};
}

} // namespace teserra::audio
