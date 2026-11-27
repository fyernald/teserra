// AudioEngine with the offline output: voices are mixed on demand with render(), so the tests check the mixed
// samples themselves. Clips hold constant values, which every resampler keeps, so the checks are exact up to
// float rounding away from a clip's ends.

#include <teserra/audio/audio_engine.hpp>

#include <teserra/test/assert.hpp>
#include <teserra/test/log_capture.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <optional>
#include <ranges>
#include <vector>

using namespace teserra;
using namespace teserra::audio;
using Catch::Approx;

namespace {

constexpr u32 kRate = 48'000;

std::unique_ptr<AudioEngine> offlineEngine(u32 maxVoices = 32) {
    auto engine = AudioEngine::create({.output = Output::Offline, .maxVoices = maxVoices});
    REQUIRE(engine.has_value());
    return std::move(*engine);
}

// A mono clip of `frames` frames, every sample `value`.
AudioClip constantClip(f32 value, usize frames, u32 sampleRate = kRate) {
    const std::vector<f32> samples(frames, value);
    return *AudioClip::create(sampleRate, 1, samples);
}

// Mixes `frames` stereo frames.
std::vector<f32> render(AudioEngine& engine, usize frames) {
    std::vector<f32> mix(frames * 2);
    engine.render(mix);
    return mix;
}

f32 left(const std::vector<f32>& mix, usize frame) {
    return mix[frame * 2];
}

f32 right(const std::vector<f32>& mix, usize frame) {
    return mix[(frame * 2) + 1];
}

bool isSilent(const std::vector<f32>& mix) {
    return std::ranges::all_of(mix, [](f32 sample) { return sample == 0.0F; });
}

} // namespace

TEST_CASE("AudioEngine: the offline output mixes silence without voices", "[audio][engine]") {
    const auto engine = offlineEngine();
    CHECK(engine->output() == Output::Offline);
    CHECK(engine->sampleRate() == kRate);
    CHECK(engine->channels() == 2);
    CHECK(engine->maxVoices() == 32);
    CHECK(engine->voiceCount() == 0);
    CHECK(isSilent(render(*engine, 256)));
}

TEST_CASE("AudioEngine: a voice plays its clip at its volume", "[audio][engine]") {
    const auto engine = offlineEngine();
    const AudioClip clip = constantClip(0.8F, 4800);
    const VoiceHandle voice = engine->play(clip, {.volume = 0.5F});
    REQUIRE_FALSE(voice.isNull());
    CHECK(engine->isPlaying(voice));
    CHECK(engine->voiceCount() == 1);

    const std::vector<f32> mix = render(*engine, 1024);
    // A mono clip goes to both channels.
    CHECK(left(mix, 512) == Approx(0.4F));
    CHECK(right(mix, 512) == Approx(0.4F));
}

TEST_CASE("AudioEngine: voices add up", "[audio][engine]") {
    const auto engine = offlineEngine();
    const AudioClip clip = constantClip(0.25F, 4800);
    engine->play(clip);
    engine->play(clip, {.volume = 0.5F});
    const std::vector<f32> mix = render(*engine, 1024);
    CHECK(left(mix, 512) == Approx(0.375F));
}

TEST_CASE("AudioEngine: pan moves a voice between the channels", "[audio][engine]") {
    const auto engine = offlineEngine();
    const AudioClip clip = constantClip(0.5F, 4800);
    engine->play(clip, {.pan = 1.0F});
    std::vector<f32> mix = render(*engine, 1024);
    CHECK(left(mix, 512) == Approx(0.0F).margin(1e-6));
    CHECK(right(mix, 512) > 0.4F);

    engine->stopAll();
    engine->play(clip, {.pan = -1.0F});
    mix = render(*engine, 1024);
    CHECK(left(mix, 512) > 0.4F);
    CHECK(right(mix, 512) == Approx(0.0F).margin(1e-6));
}

TEST_CASE("AudioEngine: a voice ends with its clip, and update() reclaims it", "[audio][engine]") {
    const auto engine = offlineEngine();
    const AudioClip clip = constantClip(0.5F, 1000);
    const VoiceHandle voice = engine->play(clip);
    const std::vector<f32> mix = render(*engine, 2000);
    CHECK(left(mix, 500) == Approx(0.5F));
    CHECK(left(mix, 1500) == 0.0F);
    CHECK_FALSE(engine->isPlaying(voice));
    // Ended, but not reclaimed yet.
    CHECK(engine->voiceCount() == 1);

    engine->update();
    CHECK(engine->voiceCount() == 0);
    CHECK_FALSE(engine->isPlaying(voice));
}

TEST_CASE("AudioEngine: a looping voice wraps around its clip", "[audio][engine]") {
    const auto engine = offlineEngine();
    const AudioClip clip = constantClip(0.5F, 100);
    const VoiceHandle voice = engine->play(clip, {.loop = true});
    const std::vector<f32> mix = render(*engine, 1000);
    for (usize frame = 50; frame < 1000; frame += 37) {
        CHECK(left(mix, frame) == Approx(0.5F));
    }
    engine->update();
    CHECK(engine->isPlaying(voice));

    // Looping off: it ends at the end of the clip.
    engine->setLooping(voice, false);
    render(*engine, 200);
    CHECK_FALSE(engine->isPlaying(voice));
}

TEST_CASE("AudioEngine: a stopped voice is silent at once and its handle is stale", "[audio][engine]") {
    const auto engine = offlineEngine();
    const AudioClip clip = constantClip(0.5F, 48'000);
    const VoiceHandle voice = engine->play(clip, {.loop = true});
    render(*engine, 256);
    engine->stop(voice);
    CHECK_FALSE(engine->isPlaying(voice));
    CHECK(engine->voiceCount() == 0);
    CHECK(isSilent(render(*engine, 256)));

    // Stale and null handles are ignored.
    engine->stop(voice);
    engine->setVolume(voice, 1.0F);
    engine->setPitch(voice, 2.0F);
    engine->setPan(voice, 1.0F);
    engine->setLooping(voice, true);
    engine->stop(VoiceHandle{});
    CHECK_FALSE(engine->isPlaying(VoiceHandle{}));

    // The slot's next voice has a new handle; the old one stays stale.
    const VoiceHandle next = engine->play(clip);
    CHECK(next != voice);
    CHECK(engine->isPlaying(next));
    CHECK_FALSE(engine->isPlaying(voice));
}

TEST_CASE("AudioEngine: setVolume() changes a playing voice", "[audio][engine]") {
    const auto engine = offlineEngine();
    const AudioClip clip = constantClip(0.8F, 48'000);
    const VoiceHandle voice = engine->play(clip);
    CHECK(left(render(*engine, 512), 256) == Approx(0.8F));
    engine->setVolume(voice, 0.25F);
    CHECK(left(render(*engine, 512), 256) == Approx(0.2F));
}

TEST_CASE("AudioEngine: pitch 2 plays a clip in half the frames", "[audio][engine]") {
    const auto engine = offlineEngine();
    const AudioClip clip = constantClip(0.5F, 4800);
    const VoiceHandle voice = engine->play(clip, {.pitch = 2.0F});
    render(*engine, 2300);
    CHECK(engine->isPlaying(voice));
    render(*engine, 200);
    CHECK_FALSE(engine->isPlaying(voice));
}

TEST_CASE("AudioEngine: clips at other sample rates are resampled", "[audio][engine]") {
    const auto engine = offlineEngine();
    // 1200 frames at 24 kHz last 2400 frames at 48 kHz.
    const AudioClip clip = constantClip(0.5F, 1200, 24'000);
    const VoiceHandle voice = engine->play(clip);
    const std::vector<f32> mix = render(*engine, 2300);
    CHECK(left(mix, 1800) == Approx(0.5F));
    CHECK(engine->isPlaying(voice));
    render(*engine, 200);
    CHECK_FALSE(engine->isPlaying(voice));
}

TEST_CASE("AudioEngine: a clip released while it plays finishes", "[audio][engine]") {
    const auto engine = offlineEngine();
    VoiceHandle voice;
    {
        const AudioClip clip = constantClip(0.5F, 1000);
        voice = engine->play(clip);
    } // the voice holds the last reference to the samples
    std::vector<f32> mix = render(*engine, 600);
    CHECK(left(mix, 300) == Approx(0.5F));
    mix = render(*engine, 600);
    CHECK(left(mix, 300) == Approx(0.5F)); // frame 900
    CHECK(left(mix, 500) == 0.0F);         // frame 1100
    engine->update();
    CHECK(engine->voiceCount() == 0);
    CHECK_FALSE(engine->isPlaying(voice));
}

TEST_CASE("AudioEngine: with every voice busy, play() returns a null handle and warns once",
          "[audio][engine]") {
    const auto engine = offlineEngine(2);
    const AudioClip clip = constantClip(0.1F, 48'000);
    const VoiceHandle first = engine->play(clip);
    const VoiceHandle second = engine->play(clip);
    REQUIRE_FALSE(first.isNull());
    REQUIRE_FALSE(second.isNull());

    const test::LogCapture capture;
    CHECK(engine->play(clip).isNull());
    CHECK(engine->play(clip).isNull());
    const auto warnings = std::ranges::count_if(
        capture.records(), [](const test::CapturedLog& record) { return record.level == LogLevel::Warn; });
    CHECK(warnings == 1);

    // A free voice plays again.
    engine->stop(first);
    CHECK_FALSE(engine->play(clip).isNull());
}

TEST_CASE("AudioEngine: an empty clip plays nothing", "[audio][engine]") {
    const auto engine = offlineEngine();
    CHECK(engine->play(AudioClip{}).isNull());
    CHECK(engine->voiceCount() == 0);
}

TEST_CASE("AudioEngine::create() refuses bad configs", "[audio][engine]") {
    CHECK(AudioEngine::create({.output = Output::Offline, .channels = 0}).error().code ==
          ErrorCode::InvalidArgument);
    CHECK(AudioEngine::create({.output = Output::Offline, .channels = 6}).error().code ==
          ErrorCode::InvalidArgument);
    CHECK(AudioEngine::create({.output = Output::Offline, .sampleRate = 1'000}).error().code ==
          ErrorCode::InvalidArgument);
    CHECK(AudioEngine::create({.output = Output::Offline, .maxVoices = 0}).error().code ==
          ErrorCode::InvalidArgument);
}

TEST_CASE("AudioEngine: a mono mix", "[audio][engine]") {
    auto engine = *AudioEngine::create({.output = Output::Offline, .channels = 1});
    engine->play(constantClip(0.5F, 4800), {.volume = 0.5F});
    std::vector<f32> mix(512);
    engine->render(mix);
    CHECK(mix[256] == Approx(0.25F));
}

#if TESERRA_ENABLE_ASSERTS
TEST_CASE("AudioEngine: misuse asserts", "[audio][engine]") {
    const auto engine = offlineEngine();
    const AudioClip clip = constantClip(0.5F, 100);
    REQUIRE_ASSERTS(engine->play(clip, {.pitch = 0.0F}));
    std::vector<f32> odd(3);
    REQUIRE_ASSERTS(engine->render(odd));
}
#endif

TEST_CASE("AudioEngine: bus volumes multiply with Master's", "[audio][engine][bus]") {
    const auto engine = offlineEngine();
    CHECK(engine->busVolume(Bus::Music) == 1.0F);
    engine->play(constantClip(0.8F, 48'000), {.bus = Bus::Music});
    engine->setBusVolume(Bus::Music, 0.5F);
    engine->setBusVolume(Bus::Master, 0.5F);
    CHECK(engine->busVolume(Bus::Music) == 0.5F);
    CHECK(left(render(*engine, 512), 256) == Approx(0.2F));
}

TEST_CASE("AudioEngine: a muted bus silences its voices only", "[audio][engine][bus]") {
    const auto engine = offlineEngine();
    engine->play(constantClip(0.25F, 48'000), {.bus = Bus::Music});
    const VoiceHandle effect = engine->play(constantClip(0.5F, 48'000)); // Effects by default
    engine->setBusMuted(Bus::Effects, true);
    CHECK(engine->isBusMuted(Bus::Effects));
    CHECK(left(render(*engine, 512), 256) == Approx(0.25F));
    // Muted voices keep playing, and come back with the bus's volume.
    CHECK(engine->isPlaying(effect));
    engine->setBusVolume(Bus::Effects, 0.5F);
    engine->setBusMuted(Bus::Effects, false);
    CHECK(left(render(*engine, 512), 256) == Approx(0.5F));

    engine->setBusMuted(Bus::Master, true);
    CHECK(isSilent(render(*engine, 512)));
}

TEST_CASE("AudioEngine: voices on Master skip the other buses", "[audio][engine][bus]") {
    const auto engine = offlineEngine();
    engine->play(constantClip(0.5F, 48'000), {.bus = Bus::Master});
    engine->setBusMuted(Bus::Music, true);
    engine->setBusMuted(Bus::Effects, true);
    CHECK(left(render(*engine, 512), 256) == Approx(0.5F));
    CHECK(busName(Bus::Effects) == "Effects");
}
