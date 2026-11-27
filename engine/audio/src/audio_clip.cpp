#include <teserra/audio/audio_clip.hpp>

#include "miniaudio_config.hpp"

#include <array>
#include <utility>

namespace teserra::audio {

namespace {

constexpr u32 kMinSampleRate = 8'000;
constexpr u32 kMaxSampleRate = 384'000;
// Frames decoded per read.
constexpr ma_uint64 kChunkFrames = 4'096;

// A miniaudio decoder over a file in memory, at the file's own format but with f32 samples.
class Decoder {
public:
    Decoder() = default;
    Decoder(const Decoder&) = delete;
    Decoder& operator=(const Decoder&) = delete;
    Decoder(Decoder&&) = delete;
    Decoder& operator=(Decoder&&) = delete;

    ~Decoder() {
        if (m_ready) {
            ma_decoder_uninit(&m_decoder);
        }
    }

    // Opens `file` and checks its format: mono or stereo, at a sample rate the engine plays.
    Result<void> open(std::span<const std::byte> file) {
        if (file.empty()) {
            return fail(ErrorCode::InvalidData, Module::Audio, "the file is empty");
        }
        ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
        config.allocationCallbacks = detail::allocationCallbacks();
        const ma_result result = ma_decoder_init_memory(file.data(), file.size(), &config, &m_decoder);
        if (result != MA_SUCCESS) {
            return fail(ErrorCode::InvalidData, Module::Audio,
                        "not a WAV or OGG Vorbis file, or a malformed one ({})",
                        ma_result_description(result));
        }
        m_ready = true;
        ma_format format = ma_format_unknown;
        ma_decoder_get_data_format(&m_decoder, &format, &m_channels, &m_sampleRate, nullptr, 0);
        if (m_channels == 0 || m_sampleRate == 0) {
            return fail(ErrorCode::InvalidData, Module::Audio, "the file has {} channels at {} Hz",
                        m_channels, m_sampleRate);
        }
        if (m_channels > AudioClip::kMaxChannels) {
            return fail(ErrorCode::Unsupported, Module::Audio,
                        "the file has {} channels; clips are mono or stereo", m_channels);
        }
        if (m_sampleRate < kMinSampleRate || m_sampleRate > kMaxSampleRate) {
            return fail(ErrorCode::Unsupported, Module::Audio,
                        "the file's sample rate is {} Hz, not {} Hz to {} Hz", m_sampleRate, kMinSampleRate,
                        kMaxSampleRate);
        }
        // 0 when the decoder cannot tell before decoding.
        if (ma_decoder_get_length_in_pcm_frames(&m_decoder, &m_length) != MA_SUCCESS) {
            m_length = 0;
        }
        return {};
    }

    // Decodes every frame. A file that ends before the length its header gives is truncated.
    Result<std::vector<f32>> decode(const AudioDecodeConfig& config) {
        const usize frameBytes = usize{m_channels} * sizeof(f32);
        const usize maxFrames = config.maxDecodedBytes / frameBytes;
        if (m_length > maxFrames) {
            return tooLong(config);
        }
        std::vector<f32> samples;
        samples.reserve(static_cast<usize>(m_length) * m_channels);
        std::vector<f32> chunk(static_cast<usize>(kChunkFrames) * m_channels);
        for (;;) {
            ma_uint64 read = 0;
            const ma_result result =
                ma_decoder_read_pcm_frames(&m_decoder, chunk.data(), kChunkFrames, &read);
            samples.insert(samples.end(), chunk.begin(),
                           chunk.begin() + static_cast<std::ptrdiff_t>(read * m_channels));
            if (samples.size() / m_channels > maxFrames) {
                return tooLong(config);
            }
            if (result == MA_AT_END || read == 0) {
                break;
            }
            if (result != MA_SUCCESS) {
                return fail(ErrorCode::InvalidData, Module::Audio, "decoding failed after {} frames ({})",
                            samples.size() / m_channels, ma_result_description(result));
            }
        }
        const u64 frames = samples.size() / m_channels;
        if (m_length != 0 && frames < m_length) {
            return fail(ErrorCode::InvalidData, Module::Audio, "the file is truncated: {} of its {} frames",
                        frames, m_length);
        }
        return samples;
    }

    [[nodiscard]] u32 channels() const noexcept { return m_channels; }
    [[nodiscard]] u32 sampleRate() const noexcept { return m_sampleRate; }
    [[nodiscard]] u64 length() const noexcept { return m_length; }

private:
    [[nodiscard]] static Unexpected tooLong(const AudioDecodeConfig& config) {
        return fail(
            ErrorCode::Unsupported, Module::Audio,
            "the decoded clip would need more than {} bytes; stream it (\"stream\": true in its sidecar)",
            config.maxDecodedBytes);
    }

    ma_decoder m_decoder{};
    bool m_ready = false;
    u32 m_channels = 0;
    u32 m_sampleRate = 0;
    ma_uint64 m_length = 0;
};

constexpr std::array<std::string_view, 2> kExtensions{"wav", "ogg"};

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
    return AudioClip{std::make_shared<const ClipData>(ClipData{.sampleRate = sampleRate,
                                                               .channels = channels,
                                                               .frameCount = samples.size() / channels,
                                                               .samples = {samples.begin(), samples.end()},
                                                               .encoded = {}})};
}

Result<AudioClip> AudioClip::stream(std::span<const std::byte> file) {
    Decoder decoder;
    TESERRA_TRY(decoder.open(file));
    return AudioClip{std::make_shared<const ClipData>(ClipData{.sampleRate = decoder.sampleRate(),
                                                               .channels = decoder.channels(),
                                                               .frameCount = decoder.length(),
                                                               .samples = {},
                                                               .encoded = {file.begin(), file.end()}})};
}

f64 AudioClip::durationSeconds() const noexcept {
    return m_data ? static_cast<f64>(m_data->frameCount) / static_cast<f64>(m_data->sampleRate) : 0.0;
}

std::span<const f32> AudioClip::samples() const noexcept {
    return m_data ? std::span<const f32>{m_data->samples} : std::span<const f32>{};
}

Result<AudioClip> decodeAudio(std::span<const std::byte> file, const AudioDecodeConfig& config) {
    Decoder decoder;
    TESERRA_TRY(decoder.open(file));
    Result<std::vector<f32>> samples = decoder.decode(config);
    if (!samples) {
        return Unexpected(std::move(samples.error()));
    }
    return AudioClip::create(decoder.sampleRate(), decoder.channels(), *samples);
}

std::span<const std::string_view> AudioClipLoader::extensions() const {
    return kExtensions;
}

Result<AudioClip> AudioClipLoader::load(assets::AssetLoadContext& context, std::span<const std::byte> bytes) {
    return context.settings().stream ? AudioClip::stream(bytes) : decodeAudio(bytes, m_config);
}

void registerAssetLoaders(assets::AssetRegistry& registry) {
    registry.registerLoader<AudioClip>(std::make_unique<AudioClipLoader>());
    registry.setFallback(AudioClip{});
}

} // namespace teserra::audio
