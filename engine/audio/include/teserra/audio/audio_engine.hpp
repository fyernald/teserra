#pragma once
// Plays clips: voices mixed by miniaudio on its audio thread, into the sound device or offline
// (docs/architecture/audio.md, ADR-0027).

#include <teserra/audio/audio_clip.hpp>
#include <teserra/core/handle.hpp>
#include <teserra/core/math.hpp>
#include <teserra/core/result.hpp>
#include <teserra/core/types.hpp>

#include <memory>
#include <span>
#include <string_view>

namespace teserra::audio {

/// A voice: one clip playing (AudioEngine::play()). Stale once the voice ends or is stopped.
using VoiceHandle = Handle<struct VoiceTag>;

/// Where the mix goes.
enum class Output : u8 {
    /// The system's default sound device; AudioEngine::create() fails without one.
    Device,
    /// Nowhere until AudioEngine::render() pulls it: for tests, tools and machines without sound.
    Offline,
};

/// Creation parameters of an AudioEngine.
struct AudioEngineConfig {
    Output output = Output::Device;
    /// The mix's sample rate; the device converts it if it runs at another.
    u32 sampleRate = 48'000;
    /// The mix's channels: 1 (mono) or 2 (stereo).
    u32 channels = 2;
    /// Voices that can play at once; play() fails beyond it.
    u32 maxVoices = 32;
};

/// A volume group that voices play into: Music and Effects feed Master, so players can turn the music down
/// without the effects (AudioEngine::setBusVolume()).
enum class Bus : u8 {
    /// Everything: Music, Effects and the voices played on Master directly.
    Master,
    Music,
    Effects,
};

/// The number of buses.
inline constexpr usize kBusCount = 3;

/// How a voice plays its clip (AudioEngine::play()).
struct PlayParams {
    /// Linear gain: 1 plays the clip as it is, 0.5 at half its amplitude.
    f32 volume = 1.0F;
    /// Speed and pitch together: 2 plays twice as fast and an octave higher. Greater than 0.
    f32 pitch = 1.0F;
    /// Stereo balance from -1 (left only) to 1 (right only); 0 leaves the clip centered.
    f32 pan = 0.0F;
    /// Starts over at the end instead of ending.
    bool loop = false;
    /// The bus the voice plays into.
    Bus bus = Bus::Effects;
    /// Positional: panned and attenuated by `position` relative to the listener (setListenerPosition()),
    /// on top of `pan`. Off, the voice plays as it is wherever the listener is.
    bool spatial = false;
    /// A spatial voice's position in world units (Y up, listener facing -Z: +X is to the right).
    Vec3 position{};
    /// A spatial voice plays at full volume up to minDistance from the listener, fades linearly, and is
    /// silent from maxDistance on.
    f32 minDistance = 1.0F;
    f32 maxDistance = 20.0F;
};

/// Mixes voices into the sound device, or offline for render(). miniaudio does the mixing, resampling and
/// panning on its own audio thread; every function here is for the main thread only.
///
///     auto engine = audio::AudioEngine::create({});                 // the default device
///     audio::VoiceHandle voice = engine->play(clip, {.volume = 0.5F, .loop = true});
///     engine->update();                                             // once per frame
///     engine->stop(voice);
///
/// Starting a voice allocates a little (miniaudio's node for it, and a decoder for a streamed clip); a
/// playing voice does not.
class AudioEngine {
public:
    /// Opens the default sound device, or prepares an offline mix. DeviceFailure when there is no device
    /// or it cannot be opened; InvalidArgument for a bad config.
    [[nodiscard]] static Result<std::unique_ptr<AudioEngine>> create(const AudioEngineConfig& config);
    /// Stops every voice and closes the device.
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;
    AudioEngine(AudioEngine&&) = delete;
    AudioEngine& operator=(AudioEngine&&) = delete;

    [[nodiscard]] Output output() const noexcept { return m_output; }
    [[nodiscard]] u32 sampleRate() const noexcept { return m_sampleRate; }
    [[nodiscard]] u32 channels() const noexcept { return m_channels; }
    [[nodiscard]] u32 maxVoices() const noexcept;
    /// Voices that have not been reclaimed: playing, or ended since the last update().
    [[nodiscard]] u32 voiceCount() const noexcept;

    /// Starts a voice of `clip` with `params`. The voice shares the clip's samples, so `clip` may be
    /// released while it plays. Returns a null handle when every voice is busy (a warning is logged
    /// once until a voice is free again) or the clip is empty.
    VoiceHandle play(const AudioClip& clip, const PlayParams& params = {});
    /// Stops a voice at once; nothing for a stale or null handle.
    void stop(VoiceHandle voice);
    /// Stops every voice.
    void stopAll();
    /// Whether the voice is still playing: false once it reached the end of a clip it does not loop, after
    /// stop(), and for stale or null handles.
    [[nodiscard]] bool isPlaying(VoiceHandle voice) const;
    // A voice's settings, as in PlayParams; nothing for a stale or null handle.
    void setVolume(VoiceHandle voice, f32 volume);
    void setPitch(VoiceHandle voice, f32 pitch);
    void setPan(VoiceHandle voice, f32 pan);
    void setLooping(VoiceHandle voice, bool loop);
    /// A voice's spatial settings, as in PlayParams; nothing for a stale or null handle.
    void setSpatial(VoiceHandle voice, bool spatial);
    void setPosition(VoiceHandle voice, Vec3 position);
    void setDistances(VoiceHandle voice, f32 minDistance, f32 maxDistance);

    /// Where spatial voices are heard from (default the origin). The listener faces -Z with +Y up, so in 2D
    /// a voice at a greater x than the listener is on the right.
    void setListenerPosition(Vec3 position);
    [[nodiscard]] Vec3 listenerPosition() const noexcept { return m_listener; }

    /// A bus's linear gain (default 1), applied to every voice of the bus; Master's applies to everything.
    void setBusVolume(Bus bus, f32 volume);
    [[nodiscard]] f32 busVolume(Bus bus) const noexcept;
    /// Silences a bus without losing its volume.
    void setBusMuted(Bus bus, bool muted);
    [[nodiscard]] bool isBusMuted(Bus bus) const noexcept;

    /// Reclaims the voices that ended, which makes their handles stale. Call it once per frame.
    void update();

    /// Mixes the next `frames.size() / channels()` frames into `frames`, interleaved. Offline output only
    /// (asserted); `frames` holds whole frames (asserted).
    void render(std::span<f32> frames);

private:
    struct Impl;

    AudioEngine(std::unique_ptr<Impl> impl, const AudioEngineConfig& config) noexcept;

    std::unique_ptr<Impl> m_impl;
    Output m_output;
    u32 m_sampleRate;
    u32 m_channels;
    Vec3 m_listener{};
};

/// The App's audio engine for scene systems: the App puts it in every scene's world as a resource.
///
///     schedule.add(Phase::Update, "coins", [](ecs::SystemContext& ctx) {
///         ctx.resource<audio::Audio>().engine->play(coinClip);
///     }).writesResource<audio::Audio>();
struct Audio {
    AudioEngine* engine = nullptr;
};

/// The name of a bus: "Master", "Music" or "Effects".
[[nodiscard]] std::string_view busName(Bus bus) noexcept;

} // namespace teserra::audio
