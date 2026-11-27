#include <teserra/audio/audio_engine.hpp>

#include <teserra/core/assert.hpp>
#include <teserra/core/handle_pool.hpp>
#include <teserra/core/log.hpp>

#include "miniaudio_config.hpp"

#include <algorithm>
#include <array>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace teserra::audio {

namespace {

constexpr u32 kMinSampleRate = 8'000;
constexpr u32 kMaxSampleRate = 384'000;
constexpr u32 kMaxVoiceLimit = 4'096;

// miniaudio's log, in the engine's; its information (the backend and device it opened) as debug messages.
// Called on the main thread and on the audio thread.
void logMessage(void* /*userData*/, ma_uint32 level, const char* message) {
    std::string_view text{message};
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    switch (level) {
        // Its errors are warnings: those that matter come back as results, which the engine reports itself,
        // and a machine without a sound device logs several while miniaudio tries its backends.
        case MA_LOG_LEVEL_ERROR:
            TESERRA_LOG_WARN(LogCategory::Audio, "miniaudio: {}", text);
            break;
        case MA_LOG_LEVEL_WARNING:
            TESERRA_LOG_DEBUG(LogCategory::Audio, "miniaudio: {}", text);
            break;
        case MA_LOG_LEVEL_INFO:
            TESERRA_LOG_DEBUG(LogCategory::Audio, "miniaudio: {}", text);
            break;
        default:
            break;
    }
}

} // namespace

// A voice slot. Its miniaudio objects are linked into the engine's node graph by address, so slots live in
// a fixed array and never move.
struct Voice {
    // The data source: the samples of a decoded clip, or a decoder of a streamed clip's file.
    ma_audio_buffer_ref buffer{};
    ma_decoder decoder{};
    bool streamed = false;
    ma_sound sound{};
    // Keeps the clip's data alive while the audio thread reads it.
    std::shared_ptr<const ClipData> data;
    VoiceHandle handle;
};

struct AudioEngine::Impl {
    explicit Impl(u32 maxVoices)
        : voices(std::make_unique<Voice[]>(maxVoices)), voiceCapacity(maxVoices) {
        freeVoices.reserve(maxVoices);
        for (u32 index = maxVoices; index > 0; --index) {
            freeVoices.push_back(index - 1);
        }
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;

    ~Impl() {
        for (BusState& bus : buses) {
            if (bus.groupReady) {
                ma_sound_group_uninit(&bus.group);
            }
        }
        if (engineReady) {
            ma_engine_uninit(&engine);
        }
        if (logReady) {
            ma_log_uninit(&log);
        }
    }

    [[nodiscard]] Voice* find(VoiceHandle handle) {
        const u32* index = handles.tryGet(handle);
        return index != nullptr ? &voices[*index] : nullptr;
    }

    [[nodiscard]] const Voice* find(VoiceHandle handle) const {
        const u32* index = handles.tryGet(handle);
        return index != nullptr ? &voices[*index] : nullptr;
    }

    // Uninitializes the voice's sound, which detaches it from the graph before the audio thread can read it
    // again, then frees its slot and its hold on the clip's data.
    void release(Voice& voice) {
        ma_sound_uninit(&voice.sound);
        releaseSource(voice);
        const u32 index = handles.get(voice.handle);
        handles.destroy(voice.handle);
        voice.handle = {};
        freeVoices.push_back(index);
        warnedFull = false;
    }

    // Opens the voice's data source for `data`.
    static Result<void> openSource(Voice& voice, const ClipData& data) {
        voice.streamed = !data.encoded.empty();
        if (!voice.streamed) {
            ma_audio_buffer_ref_init(ma_format_f32, data.channels, data.samples.data(), data.frameCount,
                                     &voice.buffer);
            voice.buffer.sampleRate = data.sampleRate;
            return {};
        }
        ma_decoder_config config = ma_decoder_config_init(ma_format_f32, data.channels, data.sampleRate);
        config.allocationCallbacks = detail::allocationCallbacks();
        const ma_result result =
            ma_decoder_init_memory(data.encoded.data(), data.encoded.size(), &config, &voice.decoder);
        if (result != MA_SUCCESS) {
            return fail(ErrorCode::InvalidData, Module::Audio, "could not decode a streamed clip: {}",
                        ma_result_description(result));
        }
        return {};
    }

    static void releaseSource(Voice& voice) {
        if (voice.streamed) {
            ma_decoder_uninit(&voice.decoder);
        } else {
            ma_audio_buffer_ref_uninit(&voice.buffer);
        }
        voice.data.reset();
    }

    [[nodiscard]] static ma_data_source* source(Voice& voice) {
        return voice.streamed ? static_cast<ma_data_source*>(&voice.decoder)
                              : static_cast<ma_data_source*>(&voice.buffer);
    }

    // Applies a bus's volume and mute to miniaudio: the engine's master volume for Master, a group otherwise.
    void applyBus(Bus bus) {
        BusState& state = buses[static_cast<usize>(bus)];
        const f32 gain = state.muted ? 0.0F : state.volume;
        if (bus == Bus::Master) {
            ma_engine_set_volume(&engine, gain);
        } else {
            ma_sound_group_set_volume(&state.group, gain);
        }
    }

    // The group that voices of `bus` play into; nullptr for Master: the engine's endpoint.
    [[nodiscard]] ma_sound_group* group(Bus bus) {
        return bus == Bus::Master ? nullptr : &buses[static_cast<usize>(bus)].group;
    }

    struct BusState {
        ma_sound_group group{};
        bool groupReady = false;
        f32 volume = 1.0F;
        bool muted = false;
    };

    ma_log log{};
    bool logReady = false;
    ma_engine engine{};
    bool engineReady = false;
    std::unique_ptr<Voice[]> voices;
    u32 voiceCapacity;
    // Indices of free slots of `voices`, the next one last.
    std::vector<u32> freeVoices;
    // A voice's handle refers to its slot index.
    HandlePool<u32, VoiceTag> handles;
    // Whether the "every voice is busy" warning was logged since a voice was last freed.
    bool warnedFull = false;
    // Declared after the engine, so the groups are uninitialized first (~Impl does it explicitly).
    std::array<BusState, kBusCount> buses{};
};

Result<std::unique_ptr<AudioEngine>> AudioEngine::create(const AudioEngineConfig& config) {
    if (config.channels == 0 || config.channels > AudioClip::kMaxChannels) {
        return fail(ErrorCode::InvalidArgument, Module::Audio,
                    "an audio engine mixes 1 or 2 channels, not {}", config.channels);
    }
    if (config.sampleRate < kMinSampleRate || config.sampleRate > kMaxSampleRate) {
        return fail(ErrorCode::InvalidArgument, Module::Audio,
                    "an audio engine's sample rate is {} Hz to {} Hz, not {} Hz", kMinSampleRate,
                    kMaxSampleRate, config.sampleRate);
    }
    if (config.maxVoices == 0 || config.maxVoices > kMaxVoiceLimit) {
        return fail(ErrorCode::InvalidArgument, Module::Audio, "an audio engine has 1 to {} voices, not {}",
                    kMaxVoiceLimit, config.maxVoices);
    }

    auto impl = std::make_unique<Impl>(config.maxVoices);
    const ma_allocation_callbacks callbacks = detail::allocationCallbacks();
    if (ma_log_init(&callbacks, &impl->log) != MA_SUCCESS) {
        return fail(ErrorCode::Internal, Module::Audio, "could not create miniaudio's log");
    }
    impl->logReady = true;
    ma_log_register_callback(&impl->log, ma_log_callback_init(logMessage, nullptr));

    ma_engine_config engineConfig = ma_engine_config_init();
    engineConfig.pLog = &impl->log;
    engineConfig.listenerCount = 1;
    engineConfig.channels = config.channels;
    engineConfig.sampleRate = config.sampleRate;
    engineConfig.allocationCallbacks = callbacks;
    engineConfig.noDevice = config.output == Output::Offline ? MA_TRUE : MA_FALSE;
    const ma_result result = ma_engine_init(&engineConfig, &impl->engine);
    if (result != MA_SUCCESS) {
        return fail(config.output == Output::Device ? ErrorCode::DeviceFailure : ErrorCode::Internal,
                    Module::Audio, "could not {}: {}",
                    config.output == Output::Device ? "open the sound device" : "create the offline mix",
                    ma_result_description(result));
    }
    impl->engineReady = true;
    for (const Bus bus : {Bus::Music, Bus::Effects}) {
        Impl::BusState& state = impl->buses[static_cast<usize>(bus)];
        const ma_result groupResult = ma_sound_group_init(&impl->engine, 0, nullptr, &state.group);
        if (groupResult != MA_SUCCESS) {
            return fail(ErrorCode::Internal, Module::Audio, "could not create the {} bus: {}", busName(bus),
                        ma_result_description(groupResult));
        }
        state.groupReady = true;
    }
    if (config.output == Output::Device) {
        const ma_device* device = ma_engine_get_device(&impl->engine);
        TESERRA_LOG_INFO(
            LogCategory::Audio, "Opened the sound device '{}': {} Hz, {} channels, mixed at {} Hz",
            device->playback.name, device->sampleRate, device->playback.channels, config.sampleRate);
    }
    return std::unique_ptr<AudioEngine>(new AudioEngine(std::move(impl), config));
}

AudioEngine::AudioEngine(std::unique_ptr<Impl> impl, const AudioEngineConfig& config) noexcept
    : m_impl(std::move(impl)),
      m_output(config.output),
      m_sampleRate(config.sampleRate),
      m_channels(config.channels) {
}

AudioEngine::~AudioEngine() {
    stopAll();
}

u32 AudioEngine::maxVoices() const noexcept {
    return m_impl->voiceCapacity;
}

u32 AudioEngine::voiceCount() const noexcept {
    return m_impl->voiceCapacity - static_cast<u32>(m_impl->freeVoices.size());
}

VoiceHandle AudioEngine::play(const AudioClip& clip, const PlayParams& params) {
    TESERRA_ASSERT(params.pitch > 0.0F, "a voice's pitch is greater than 0, not {}", params.pitch);
    if (clip.isEmpty()) {
        return {};
    }
    Impl& impl = *m_impl;
    if (impl.freeVoices.empty()) {
        if (!impl.warnedFull) {
            TESERRA_LOG_WARN(LogCategory::Audio,
                             "Every voice of the audio engine is busy ({}): a sound is not played",
                             impl.voiceCapacity);
            impl.warnedFull = true;
        }
        return {};
    }
    const u32 index = impl.freeVoices.back();
    Voice& voice = impl.voices[index];
    const std::shared_ptr<const ClipData>& data = clip.data();
    if (const Result<void> opened = Impl::openSource(voice, *data); !opened) {
        TESERRA_LOG_ERROR(LogCategory::Audio, "Could not start a voice: {}", opened.error().message);
        return {};
    }
    const ma_result result = ma_sound_init_from_data_source(&impl.engine, Impl::source(voice), 0,
                                                            impl.group(params.bus), &voice.sound);
    if (result != MA_SUCCESS) {
        Impl::releaseSource(voice);
        TESERRA_LOG_ERROR(LogCategory::Audio, "Could not start a voice: {}", ma_result_description(result));
        return {};
    }
    impl.freeVoices.pop_back();
    voice.data = data;
    voice.handle = impl.handles.create(index);
    ma_sound_set_volume(&voice.sound, std::max(params.volume, 0.0F));
    ma_sound_set_pitch(&voice.sound, params.pitch);
    ma_sound_set_pan(&voice.sound, std::clamp(params.pan, -1.0F, 1.0F));
    ma_sound_set_looping(&voice.sound, params.loop ? MA_TRUE : MA_FALSE);
    // Positional sound: linear distance attenuation, no Doppler (a moving 2D source would change pitch).
    ma_sound_set_attenuation_model(&voice.sound, ma_attenuation_model_linear);
    ma_sound_set_rolloff(&voice.sound, 1.0F);
    ma_sound_set_doppler_factor(&voice.sound, 0.0F);
    setDistances(voice.handle, params.minDistance, params.maxDistance);
    ma_sound_set_position(&voice.sound, params.position.x, params.position.y, params.position.z);
    ma_sound_set_spatialization_enabled(&voice.sound, params.spatial ? MA_TRUE : MA_FALSE);
    ma_sound_start(&voice.sound);
    return voice.handle;
}

void AudioEngine::stop(VoiceHandle voice) {
    if (Voice* found = m_impl->find(voice)) {
        m_impl->release(*found);
    }
}

void AudioEngine::stopAll() {
    for (u32 index = 0; index < m_impl->voiceCapacity; ++index) {
        Voice& voice = m_impl->voices[index];
        if (!voice.handle.isNull()) {
            m_impl->release(voice);
        }
    }
}

bool AudioEngine::isPlaying(VoiceHandle voice) const {
    const Voice* found = m_impl->find(voice);
    return found != nullptr && ma_sound_at_end(&found->sound) == MA_FALSE;
}

void AudioEngine::setVolume(VoiceHandle voice, f32 volume) {
    if (Voice* found = m_impl->find(voice)) {
        ma_sound_set_volume(&found->sound, std::max(volume, 0.0F));
    }
}

void AudioEngine::setPitch(VoiceHandle voice, f32 pitch) {
    TESERRA_ASSERT(pitch > 0.0F, "a voice's pitch is greater than 0, not {}", pitch);
    if (Voice* found = m_impl->find(voice)) {
        ma_sound_set_pitch(&found->sound, pitch);
    }
}

void AudioEngine::setPan(VoiceHandle voice, f32 pan) {
    if (Voice* found = m_impl->find(voice)) {
        ma_sound_set_pan(&found->sound, std::clamp(pan, -1.0F, 1.0F));
    }
}

void AudioEngine::setLooping(VoiceHandle voice, bool loop) {
    if (Voice* found = m_impl->find(voice)) {
        ma_sound_set_looping(&found->sound, loop ? MA_TRUE : MA_FALSE);
    }
}

void AudioEngine::setSpatial(VoiceHandle voice, bool spatial) {
    if (Voice* found = m_impl->find(voice)) {
        ma_sound_set_spatialization_enabled(&found->sound, spatial ? MA_TRUE : MA_FALSE);
    }
}

void AudioEngine::setPosition(VoiceHandle voice, Vec3 position) {
    if (Voice* found = m_impl->find(voice)) {
        ma_sound_set_position(&found->sound, position.x, position.y, position.z);
    }
}

void AudioEngine::setDistances(VoiceHandle voice, f32 minDistance, f32 maxDistance) {
    if (Voice* found = m_impl->find(voice)) {
        const f32 nearest = std::max(minDistance, 0.0F);
        ma_sound_set_min_distance(&found->sound, nearest);
        ma_sound_set_max_distance(&found->sound, std::max(maxDistance, nearest + 1e-3F));
    }
}

void AudioEngine::setListenerPosition(Vec3 position) {
    m_listener = position;
    ma_engine_listener_set_position(&m_impl->engine, 0, position.x, position.y, position.z);
}

void AudioEngine::setBusVolume(Bus bus, f32 volume) {
    m_impl->buses[static_cast<usize>(bus)].volume = std::max(volume, 0.0F);
    m_impl->applyBus(bus);
}

f32 AudioEngine::busVolume(Bus bus) const noexcept {
    return m_impl->buses[static_cast<usize>(bus)].volume;
}

void AudioEngine::setBusMuted(Bus bus, bool muted) {
    m_impl->buses[static_cast<usize>(bus)].muted = muted;
    m_impl->applyBus(bus);
}

bool AudioEngine::isBusMuted(Bus bus) const noexcept {
    return m_impl->buses[static_cast<usize>(bus)].muted;
}

std::string_view busName(Bus bus) noexcept {
    switch (bus) {
        case Bus::Master:
            return "Master";
        case Bus::Music:
            return "Music";
        case Bus::Effects:
            return "Effects";
    }
    return "Unknown";
}

void AudioEngine::update() {
    for (u32 index = 0; index < m_impl->voiceCapacity; ++index) {
        Voice& voice = m_impl->voices[index];
        if (!voice.handle.isNull() && ma_sound_at_end(&voice.sound) == MA_TRUE) {
            m_impl->release(voice);
        }
    }
}

void AudioEngine::render(std::span<f32> frames) {
    TESERRA_ASSERT(m_output == Output::Offline, "AudioEngine::render() needs the offline output");
    TESERRA_ASSERT(frames.size() % m_channels == 0, "{} samples are not whole frames of {} channels",
                   frames.size(), m_channels);
    const ma_uint64 frameCount = frames.size() / m_channels;
    ma_uint64 framesRead = 0;
    ma_engine_read_pcm_frames(&m_impl->engine, frames.data(), frameCount, &framesRead);
    std::fill(frames.begin() + static_cast<std::ptrdiff_t>(framesRead * m_channels), frames.end(), 0.0F);
}

} // namespace teserra::audio
