// AudioEngine with the sound device. CI machines have none, so the test skips there; it runs on developer
// machines.

#include <teserra/audio/audio_engine.hpp>
#include <teserra/test/assert.hpp>

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace teserra;
using namespace teserra::audio;

TEST_CASE("AudioEngine: the default sound device opens and plays", "[audio][device]") {
    auto engine = AudioEngine::create({.output = Output::Device});
    if (!engine.has_value()) {
        CHECK(engine.error().code == ErrorCode::DeviceFailure);
        SKIP("No sound device: " << engine.error().message);
    }
    CHECK((*engine)->output() == Output::Device);
    // A short, quiet clip; the test does not wait for it.
    const std::vector<f32> silence(480, 0.0F);
    const VoiceHandle voice = (*engine)->play(*AudioClip::create(48'000, 1, silence));
    CHECK_FALSE(voice.isNull());
#if TESERRA_ENABLE_ASSERTS
    std::vector<f32> frames(64);
    REQUIRE_ASSERTS((*engine)->render(frames));
#endif
}
