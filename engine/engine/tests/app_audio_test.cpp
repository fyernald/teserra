// The App's audio engine: offline with audio disabled (AppConfig or --no-audio), the device or a warned
// offline fallback otherwise, clip assets through the App's registry, and the audio::Audio resource of
// scenes.

#include <teserra/audio/audio_clip.hpp>
#include <teserra/audio/audio_engine.hpp>
#include <teserra/engine/app.hpp>
#include <teserra/test/log_capture.hpp>
#include <teserra/test/memory_file_system.hpp>

#include "headless_platform.hpp"
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

using namespace teserra;
using namespace teserra::engine;

namespace {

AppConfig headlessConfig(bool audioEnabled = false) {
    return {
        .title = "Audio test", .backend = std::nullopt, .maxFrames = 1, .audio = {.enabled = audioEnabled}};
}

usize warningsAbout(const test::LogCapture& logs, LogCategory category) {
    return static_cast<usize>(std::ranges::count_if(logs.records(), [&](const test::CapturedLog& record) {
        return record.level == LogLevel::Warn && record.category == category;
    }));
}

// A small mono 16-bit WAV file of `frames` frames of silence.
std::vector<std::byte> silentWav(u32 frames) {
    std::vector<std::byte> file;
    const auto put = [&](u32 value, int bytes) {
        for (int i = 0; i < bytes; ++i) {
            file.push_back(static_cast<std::byte>((value >> (8 * i)) & 0xFFU));
        }
    };
    const auto tag = [&](const char* text) {
        for (int i = 0; i < 4; ++i) {
            file.push_back(static_cast<std::byte>(text[i]));
        }
    };
    tag("RIFF");
    put(36 + (frames * 2), 4);
    tag("WAVE");
    tag("fmt ");
    put(16, 4);
    put(1, 2);          // PCM
    put(1, 2);          // mono
    put(48'000, 4);     // sample rate
    put(48'000 * 2, 4); // bytes per second
    put(2, 2);          // block align
    put(16, 2);         // bits per sample
    tag("data");
    put(frames * 2, 4);
    file.resize(file.size() + (frames * 2), std::byte{0});
    return file;
}

} // namespace

TEST_CASE("App: with audio disabled, the audio engine mixes offline without a warning",
          "[engine][app][audio]") {
    const test::LogCapture logs;
    App app(headlessConfig(false), std::make_unique<test::HeadlessPlatform>());
    REQUIRE(app.isInitialized());
    CHECK(app.audio().output() == audio::Output::Offline);
    CHECK(app.audio().maxVoices() == 32);
    CHECK(warningsAbout(logs, LogCategory::Audio) == 0);
}

TEST_CASE("App: --no-audio disables audio", "[engine][app][audio]") {
    const std::array<const char*, 2> argv{"game", "--no-audio"};
    AppConfig config = headlessConfig(true);
    config.args = {.argc = static_cast<int>(argv.size()), .argv = argv.data()};
    App app(config, std::make_unique<test::HeadlessPlatform>());
    REQUIRE(app.isInitialized());
    CHECK_FALSE(app.config().audio.enabled);
    CHECK(app.audio().output() == audio::Output::Offline);
}

TEST_CASE("App: with audio enabled, the device opens, or the App runs offline with one warning",
          "[engine][app][audio]") {
    const test::LogCapture logs;
    App app(headlessConfig(true), std::make_unique<test::HeadlessPlatform>());
    REQUIRE(app.isInitialized());
    if (app.audio().output() == audio::Output::Offline) {
        // No sound device (CI): the game still runs.
        CHECK(warningsAbout(logs, LogCategory::Audio) >= 1);
        CHECK(std::ranges::any_of(logs.records(), [](const test::CapturedLog& record) {
            return record.level == LogLevel::Warn && record.message.find("No sound") != std::string::npos;
        }));
    }
    CHECK(app.run() == 0);
}

TEST_CASE("App: an invalid audio setting fails initialization", "[engine][app][audio]") {
    AppConfig config = headlessConfig(false);
    config.audio.channels = 6;
    const App app(config, std::make_unique<test::HeadlessPlatform>());
    REQUIRE_FALSE(app.isInitialized());
    CHECK(app.initError()->code == ErrorCode::InvalidArgument);
}

TEST_CASE("App: clips load through the App's assets", "[engine][app][audio]") {
    auto platform = std::make_unique<test::HeadlessPlatform>();
    test::MemoryFileSystem& files =
        platform->useFileSystem(std::make_unique<test::MemoryFileSystem>("/game"));
    files.addFile("/game/assets/coin.wav", silentWav(480));
    App app(headlessConfig(false), std::move(platform));
    REQUIRE(app.isInitialized());
    {
        const assets::Asset<audio::AudioClip> coin = app.assets().load<audio::AudioClip>("assets://coin.wav");
        REQUIRE(coin.isLoaded());
        CHECK(coin.get().frameCount() == 480);
        // A missing clip falls back to silence.
        const assets::Asset<audio::AudioClip> missing =
            app.assets().load<audio::AudioClip>("assets://none.wav");
        CHECK(missing.isFallback());
    }
    app.assets().endFrame();
}

TEST_CASE("App: scene systems play sounds through the audio::Audio resource", "[engine][app][audio]") {
    AppConfig config = headlessConfig(false);
    config.maxFrames = 3;
    App app(config, std::make_unique<test::HeadlessPlatform>());
    REQUIRE(app.isInitialized());
    const std::vector<f32> samples(48'000, 0.0F);
    const audio::AudioClip clip = *audio::AudioClip::create(48'000, 1, samples);
    scene::Scene& level = app.scenes().create("level");
    std::vector<u32> voicesSeen;
    level.systems()
        .add(ecs::Phase::Update, "play",
             [&](ecs::SystemContext& ctx) {
                 audio::AudioEngine& engine = *ctx.resource<audio::Audio>().engine;
                 if (app.frameCount() == 0) {
                     CHECK_FALSE(engine.play(clip, {.bus = audio::Bus::Effects}).isNull());
                 }
                 voicesSeen.push_back(engine.voiceCount());
             })
        .writesResource<audio::Audio>();
    CHECK(app.run(level) == 0);
    CHECK(&*level.world().resource<audio::Audio>().engine == &app.audio());
    // Offline output that nothing pulls: the voice stays, and the App does not reclaim a playing voice.
    CHECK(voicesSeen == std::vector<u32>{1, 1, 1});
}
