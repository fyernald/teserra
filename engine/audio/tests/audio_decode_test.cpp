// Clip files: WAV and OGG Vorbis decoded to exact (WAV) or close (Vorbis, lossy) samples, streamed clips that
// mix like decoded ones, the errors, and Asset<AudioClip> through the loader and its sidecar.
//
// WAV files are written by test::writeWav(); tone.ogg comes from tools/scripts/make_audio_fixtures.py: 0.5 s
// at 44.1 kHz of a 440 Hz sine, 0.5 on the left and 0.25 on the right.

#include <teserra/assets/asset_registry.hpp>
#include <teserra/assets/mount_table.hpp>
#include <teserra/audio/audio_clip.hpp>
#include <teserra/audio/audio_engine.hpp>
#include <teserra/test/log_capture.hpp>
#include <teserra/test/memory_file_system.hpp>

#include "wav.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

using namespace teserra;
using namespace teserra::audio;
using Catch::Approx;
using Catch::Matchers::ContainsSubstring;
using test::WavFormat;
using test::writeWav;

namespace {

constexpr u32 kToneRate = 44'100;
constexpr u64 kToneFrames = 22'050;

std::vector<std::byte> readFixture(const std::string& name) {
    std::ifstream file(std::string(TESERRA_AUDIO_TEST_DATA) + "/" + name, std::ios::binary);
    REQUIRE(file.is_open());
    const std::vector<char> chars{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    std::vector<std::byte> bytes(chars.size());
    std::ranges::transform(chars, bytes.begin(), [](char c) { return static_cast<std::byte>(c); });
    return bytes;
}

f32 expectedTone(u64 frame, u32 channel) {
    const f64 wave = std::sin(2.0 * std::numbers::pi * 440.0 * static_cast<f64>(frame) / kToneRate);
    return static_cast<f32>((channel == 0 ? 0.5 : 0.25) * wave);
}

// Mixes `frames` stereo frames of `clip` started at frame 0 and, if `secondAt` is not 0, again at `secondAt`.
std::vector<f32> mixOf(const AudioClip& clip, usize frames, usize secondAt = 0) {
    auto engine = *AudioEngine::create({.output = Output::Offline, .sampleRate = kToneRate});
    std::vector<f32> mix(frames * 2);
    engine->play(clip);
    const usize first = secondAt == 0 ? frames : secondAt;
    engine->render(std::span<f32>(mix).first(first * 2));
    if (secondAt != 0) {
        engine->play(clip);
        engine->render(std::span<f32>(mix).subspan(first * 2));
    }
    return mix;
}

// The error of a result that must have failed.
template <typename T>
Error errorOf(const Result<T>& result) {
    REQUIRE_FALSE(result.has_value());
    return result.error();
}

f32 maxDifference(const std::vector<f32>& a, const std::vector<f32>& b) {
    f32 difference = 0.0F;
    for (usize i = 0; i < std::min(a.size(), b.size()); ++i) {
        difference = std::max(difference, std::abs(a[i] - b[i]));
    }
    return difference;
}

} // namespace

TEST_CASE("decodeAudio: a 16-bit mono WAV decodes to its exact samples", "[audio][decode]") {
    std::vector<f32> samples;
    for (int i = -500; i < 500; ++i) {
        samples.push_back(static_cast<f32>(i * 37) / 32768.0F);
    }
    const AudioClip clip = *decodeAudio(writeWav(WavFormat::Pcm16, 22'050, 1, samples));
    CHECK(clip.channels() == 1);
    CHECK(clip.sampleRate() == 22'050);
    CHECK(clip.frameCount() == samples.size());
    CHECK_FALSE(clip.isStreamed());
    CHECK(std::vector<f32>(clip.samples().begin(), clip.samples().end()) == samples);
}

TEST_CASE("decodeAudio: a float stereo WAV decodes to its exact samples", "[audio][decode]") {
    std::vector<f32> samples;
    for (int frame = 0; frame < 300; ++frame) {
        samples.push_back(0.5F * std::sin(static_cast<f32>(frame) * 0.1F));
        samples.push_back(-0.25F);
    }
    const AudioClip clip = *decodeAudio(writeWav(WavFormat::Float32, 48'000, 2, samples));
    CHECK(clip.channels() == 2);
    CHECK(clip.frameCount() == 300);
    CHECK(std::vector<f32>(clip.samples().begin(), clip.samples().end()) == samples);
}

TEST_CASE("decodeAudio: an OGG Vorbis file decodes close to its tone", "[audio][decode]") {
    const AudioClip clip = *decodeAudio(readFixture("tone.ogg"));
    CHECK(clip.channels() == 2);
    CHECK(clip.sampleRate() == kToneRate);
    CHECK(clip.frameCount() == kToneFrames);
    const std::span<const f32> samples = clip.samples();
    // Vorbis is lossy: close, not exact.
    for (u64 frame = 1'000; frame < kToneFrames - 1'000; frame += 997) {
        CHECK(samples[frame * 2] == Approx(expectedTone(frame, 0)).margin(0.02));
        CHECK(samples[(frame * 2) + 1] == Approx(expectedTone(frame, 1)).margin(0.02));
    }
}

TEST_CASE("AudioClip::stream: a streamed clip mixes like the decoded one", "[audio][decode]") {
    const std::vector<std::byte> file = readFixture("tone.ogg");
    const AudioClip decoded = *decodeAudio(file);
    const AudioClip streamed = *AudioClip::stream(file);
    CHECK(streamed.isStreamed());
    CHECK(streamed.samples().empty());
    CHECK(streamed.channels() == 2);
    CHECK(streamed.sampleRate() == kToneRate);

    const std::vector<f32> fromMemory = mixOf(decoded, 8'000);
    const std::vector<f32> fromStream = mixOf(streamed, 8'000);
    CHECK(maxDifference(fromMemory, fromStream) < 1e-5F);
    CHECK(std::ranges::any_of(fromStream, [](f32 sample) { return std::abs(sample) > 0.4F; }));
}

TEST_CASE("AudioClip::stream: two voices of one streamed clip play independently", "[audio][decode]") {
    const std::vector<std::byte> file = readFixture("tone.ogg");
    const std::vector<f32> fromMemory = mixOf(*decodeAudio(file), 8'000, 3'000);
    const std::vector<f32> fromStream = mixOf(*AudioClip::stream(file), 8'000, 3'000);
    CHECK(maxDifference(fromMemory, fromStream) < 1e-5F);
}

TEST_CASE("AudioClip::stream: a streamed WAV knows its length", "[audio][decode]") {
    const std::vector<f32> samples(1'000, 0.25F);
    const AudioClip clip = *AudioClip::stream(writeWav(WavFormat::Pcm16, 48'000, 1, samples));
    CHECK(clip.isStreamed());
    CHECK(clip.frameCount() == 1'000);
    CHECK(clip.durationSeconds() == Approx(1'000.0 / 48'000.0));
}

TEST_CASE("decodeAudio: empty and malformed files and cut headers are InvalidData", "[audio][decode]") {
    const std::vector<f32> samples(1'000, 0.25F);
    const std::vector<std::byte> wav = writeWav(WavFormat::Pcm16, 48'000, 1, samples);

    CHECK(errorOf(decodeAudio({})).code == ErrorCode::InvalidData);
    CHECK(errorOf(AudioClip::stream({})).code == ErrorCode::InvalidData);

    const std::vector<std::byte> header(wav.begin(), wav.begin() + 20);
    CHECK(errorOf(decodeAudio(header)).code == ErrorCode::InvalidData);

    // A WAV cut inside its data plays the frames it has: miniaudio clamps the data chunk to the file, as WAV
    // files written by streaming tools often give wrong sizes.
    const std::vector<std::byte> cut(wav.begin(), wav.end() - 100);
    CHECK(decodeAudio(cut)->frameCount() == 950);

    std::vector<std::byte> png(64, std::byte{0});
    png[1] = std::byte{'P'};
    CHECK(errorOf(decodeAudio(png)).code == ErrorCode::InvalidData);
}

TEST_CASE("decodeAudio: more than two channels are Unsupported", "[audio][decode]") {
    const std::vector<f32> samples(300, 0.1F);
    const Error error = errorOf(decodeAudio(writeWav(WavFormat::Pcm16, 48'000, 3, samples)));
    CHECK(error.code == ErrorCode::Unsupported);
    CHECK(error.module == Module::Audio);
}

TEST_CASE("decodeAudio: a clip over maxDecodedBytes is Unsupported, and can be streamed", "[audio][decode]") {
    const std::vector<f32> samples(10'000, 0.1F);
    const std::vector<std::byte> wav = writeWav(WavFormat::Pcm16, 48'000, 1, samples);
    const Error error = errorOf(decodeAudio(wav, {.maxDecodedBytes = 1'000}));
    CHECK(error.code == ErrorCode::Unsupported);
    CHECK_THAT(error.message, ContainsSubstring("stream"));
    CHECK(AudioClip::stream(wav).has_value());
}

TEST_CASE("AudioClipLoader: clips load as assets, streamed when the sidecar says so", "[audio][decode]") {
    test::MemoryFileSystem files;
    assets::MountTable mounts{files};
    REQUIRE(mounts.mount("assets", "/game/assets"));
    assets::AssetRegistry registry{{.mounts = &mounts}};
    registerAssetLoaders(registry);

    const std::vector<f32> samples(480, 0.5F);
    files.addFile("/game/assets/coin.wav", writeWav(WavFormat::Pcm16, 48'000, 1, samples));
    files.addFile("/game/assets/music.ogg", readFixture("tone.ogg"));
    files.addTextFile("/game/assets/music.ogg.asset.json", R"({"stream": true})");

    const assets::Asset<AudioClip> coin = registry.load<AudioClip>("assets://coin.wav");
    REQUIRE(coin.isLoaded());
    CHECK_FALSE(coin.get().isStreamed());
    CHECK(coin.get().frameCount() == 480);

    const assets::Asset<AudioClip> music = registry.load<AudioClip>("assets://music.ogg");
    REQUIRE(music.isLoaded());
    CHECK(music.get().isStreamed());

    // A missing file logs one error and resolves to the silent fallback.
    const test::LogCapture logs;
    const assets::Asset<AudioClip> missing = registry.load<AudioClip>("assets://missing.wav");
    CHECK(missing.isFallback());
    CHECK(missing.get().isEmpty());
    const auto errors = std::ranges::count_if(
        logs.records(), [](const test::CapturedLog& record) { return record.level == LogLevel::Error; });
    CHECK(errors == 1);
}
