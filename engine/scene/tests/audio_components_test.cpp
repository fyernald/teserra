// AudioSource and AudioListener: the audio system starts a source's voice, follows its volume and position,
// and stops it when the component, the entity or `playing` goes away, and when the scene exits or resets.
// The scene runs by hand with an offline audio engine, as the App would give it.

#include <teserra/scene/audio_components.hpp>

#include <teserra/assets/asset_registry.hpp>
#include <teserra/audio/audio_clip.hpp>
#include <teserra/audio/audio_engine.hpp>
#include <teserra/scene/scene.hpp>
#include <teserra/scene/transform.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

using namespace teserra;
using namespace teserra::scene;
using Catch::Approx;

namespace {

struct Fixture {
    Fixture() {
        level.world().insertResource<audio::Audio>(audio::Audio{.engine = engine.get()});
        // A long clip of constant 0.5, so a playing voice mixes 0.5 on both channels.
        clip = registry.load<audio::AudioClip>("test://half.clip", [] {
            const std::vector<f32> samples(96'000, 0.5F);
            return audio::AudioClip::create(48'000, 1, samples);
        });
    }

    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
    Fixture(Fixture&&) = delete;
    Fixture& operator=(Fixture&&) = delete;

    ~Fixture() {
        level.world().destroyAll();
        clip = {};
        registry.endFrame();
    }

    // Runs PostUpdate (transforms, then audio) and mixes `frames` stereo frames.
    std::vector<f32> frame(usize frames = 4'800) {
        level.run(ecs::Phase::PostUpdate);
        std::vector<f32> mix(frames * 2);
        engine->render(mix);
        return mix;
    }

    ecs::Entity source(const AudioSource& settings, Vec3 position = {}) {
        const ecs::Entity entity = level.spawn();
        level.add<Transform>(entity, Transform{.position = position});
        AudioSource withClip = settings;
        withClip.clip = clip;
        level.add<AudioSource>(entity, withClip);
        return entity;
    }

    std::unique_ptr<audio::AudioEngine> engine =
        *audio::AudioEngine::create({.output = audio::Output::Offline});
    assets::AssetRegistry registry{{}};
    assets::Asset<audio::AudioClip> clip;
    Scene level{"level"};
};

f32 lastLeft(const std::vector<f32>& mix) {
    return mix[mix.size() - 2];
}

f32 lastRight(const std::vector<f32>& mix) {
    return mix.back();
}

} // namespace

TEST_CASE("AudioSource: an added source plays, and stops when the component goes", "[scene][audio]") {
    Fixture fixture;
    const ecs::Entity speaker = fixture.source({.loop = true});
    CHECK(lastLeft(fixture.frame()) == Approx(0.5F));
    CHECK(fixture.engine->voiceCount() == 1);
    // Later frames keep the same voice.
    fixture.frame();
    CHECK(fixture.engine->voiceCount() == 1);

    fixture.level.world().remove<AudioSource>(speaker);
    CHECK(lastLeft(fixture.frame()) == 0.0F);
    CHECK(fixture.engine->voiceCount() == 0);
}

TEST_CASE("AudioSource: a destroyed entity's voice stops", "[scene][audio]") {
    Fixture fixture;
    const ecs::Entity speaker = fixture.source({.loop = true});
    fixture.frame();
    fixture.level.world().destroy(speaker);
    CHECK(lastLeft(fixture.frame()) == 0.0F);
    CHECK(fixture.engine->voiceCount() == 0);
}

TEST_CASE("AudioSource: playing off stops the voice, on starts it over", "[scene][audio]") {
    Fixture fixture;
    const ecs::Entity speaker = fixture.source({});
    fixture.frame();
    fixture.level.world().get<AudioSource>(speaker).playing = false;
    CHECK(lastLeft(fixture.frame()) == 0.0F);
    fixture.level.world().get<AudioSource>(speaker).playing = true;
    CHECK(lastLeft(fixture.frame()) == Approx(0.5F));
}

TEST_CASE("AudioSource: volume changes reach the voice the next frame", "[scene][audio]") {
    Fixture fixture;
    const ecs::Entity speaker = fixture.source({.volume = 1.0F, .loop = true});
    fixture.frame();
    fixture.level.world().get<AudioSource>(speaker).volume = 0.5F;
    CHECK(lastLeft(fixture.frame()) == Approx(0.25F));
}

TEST_CASE("AudioSource: exiting or resetting the scene stops its voices", "[scene][audio]") {
    Fixture fixture;
    fixture.source({.loop = true});
    fixture.frame();
    fixture.level.exit();
    CHECK(fixture.engine->voiceCount() == 0);
    // Still there and still playing: entering again starts it over.
    fixture.level.enter();
    CHECK(lastLeft(fixture.frame()) == Approx(0.5F));

    fixture.level.reset();
    CHECK(fixture.engine->voiceCount() == 0);
    CHECK(fixture.level.world().resource<AudioSourceVoices>().voices.empty());
}

TEST_CASE("AudioSource: spatial sources are heard from the AudioListener", "[scene][audio]") {
    Fixture fixture;
    const ecs::Entity camera = fixture.level.spawn();
    fixture.level.add<Transform>(camera, Transform{.position = {10.0F, 0.0F, 0.0F}});
    fixture.level.add<AudioListener>(camera);

    SECTION("right of the listener is louder on the right") {
        fixture.source({.loop = true, .spatial = true, .maxDistance = 20.0F}, {15.0F, 0.0F, 0.0F});
        const std::vector<f32> mix = fixture.frame();
        CHECK(fixture.engine->listenerPosition() == Vec3{10.0F, 0.0F, 0.0F});
        CHECK(lastRight(mix) > lastLeft(mix) + 0.05F);
    }
    SECTION("beyond maxDistance it is silent, and it follows its entity") {
        const ecs::Entity bee =
            fixture.source({.loop = true, .spatial = true, .maxDistance = 5.0F}, {30.0F, 0.0F, 0.0F});
        std::vector<f32> mix = fixture.frame();
        CHECK(lastLeft(mix) == Approx(0.0F).margin(1e-4));
        CHECK(lastRight(mix) == Approx(0.0F).margin(1e-4));
        // At the listener: full volume, centered.
        fixture.level.world().get<Transform>(bee).position = {10.0F, 0.0F, 0.0F};
        mix = fixture.frame();
        CHECK(lastLeft(mix) == Approx(0.5F).margin(1e-3));
        CHECK(lastRight(mix) == Approx(0.5F).margin(1e-3));
    }
}

TEST_CASE("AudioSource: a scene without the audio::Audio resource plays nothing", "[scene][audio]") {
    assets::AssetRegistry registry{{}};
    Scene level{"level"};
    {
        const ecs::Entity speaker = level.spawn();
        level.add<AudioSource>(speaker, AudioSource{});
        level.run(ecs::Phase::PostUpdate);
        CHECK(level.world().resource<AudioSourceVoices>().voices.empty());
    }
    level.world().destroyAll();
    registry.endFrame();
}
