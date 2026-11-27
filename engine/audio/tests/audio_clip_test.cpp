// AudioClip: clips made in code, their checks, and shared samples.

#include <teserra/audio/audio_clip.hpp>

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace teserra;
using namespace teserra::audio;

TEST_CASE("AudioClip::create() keeps the samples and their format", "[audio][clip]") {
    const std::vector<f32> samples{0.1F, -0.1F, 0.2F, -0.2F, 0.3F, -0.3F};
    const AudioClip clip = *AudioClip::create(24'000, 2, samples);

    CHECK(clip.sampleRate() == 24'000);
    CHECK(clip.channels() == 2);
    CHECK(clip.frameCount() == 3);
    CHECK(clip.durationSeconds() == 3.0 / 24'000.0);
    CHECK_FALSE(clip.isEmpty());
    CHECK(std::vector<f32>(clip.samples().begin(), clip.samples().end()) == samples);
}

TEST_CASE("AudioClip: copies share the samples", "[audio][clip]") {
    const AudioClip clip = *AudioClip::create(48'000, 1, std::vector<f32>{0.5F, 0.25F});
    const AudioClip copy = clip; // NOLINT(performance-unnecessary-copy-initialization): the copy is the test
    CHECK(copy.data() == clip.data());
    CHECK(copy.samples().data() == clip.samples().data());
}

TEST_CASE("AudioClip: empty clips", "[audio][clip]") {
    const AudioClip none;
    CHECK(none.isEmpty());
    CHECK(none.frameCount() == 0);
    CHECK(none.sampleRate() == 0);
    CHECK(none.samples().empty());
    CHECK(none.data() == nullptr);

    const AudioClip made = *AudioClip::create(48'000, 2, {});
    CHECK(made.isEmpty());
}

TEST_CASE("AudioClip::create() refuses bad formats", "[audio][clip]") {
    const std::vector<f32> three{0.0F, 0.0F, 0.0F};
    CHECK(AudioClip::create(48'000, 0, three).error().code == ErrorCode::InvalidArgument);
    CHECK(AudioClip::create(48'000, 3, three).error().code == ErrorCode::InvalidArgument);
    CHECK(AudioClip::create(4'000, 1, three).error().code == ErrorCode::InvalidArgument);
    CHECK(AudioClip::create(500'000, 1, three).error().code == ErrorCode::InvalidArgument);
    // Three samples are not whole stereo frames.
    const Error partial = AudioClip::create(48'000, 2, three).error();
    CHECK(partial.code == ErrorCode::InvalidArgument);
    CHECK(partial.module == Module::Audio);
}
