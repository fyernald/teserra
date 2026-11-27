#include <teserra/core/profile.hpp>
#include <teserra/ecs/system_context.hpp>
#include <teserra/ecs/world.hpp>
#include <teserra/scene/audio_components.hpp>
#include <teserra/scene/transform.hpp>

#include <iterator>

namespace teserra::scene {

namespace {

// The world position of an entity: its GlobalTransform, or its Transform before the transform system ran.
Vec3 worldPosition(ecs::SystemContext& ctx, ecs::Entity entity) {
    if (const auto* global = ctx.tryGet<const GlobalTransform>(entity)) {
        return global->translation();
    }
    if (const auto* local = ctx.tryGet<const Transform>(entity)) {
        return local->position;
    }
    return {};
}

} // namespace

void audioSystem(ecs::SystemContext& ctx) {
    TESERRA_PROFILE_SCOPE("Audio sources");
    audio::Audio* audioResource = ctx.tryResource<audio::Audio>();
    if (audioResource == nullptr || audioResource->engine == nullptr) {
        return;
    }
    audio::AudioEngine& engine = *audioResource->engine;
    auto& voices = ctx.resource<AudioSourceVoices>().voices;

    // The first listener, if there is one.
    auto listeners = ctx.view<const AudioListener>();
    if (auto first = listeners.begin(); first != listeners.end()) {
        const auto [entity, listener] = *first;
        static_cast<void>(listener);
        engine.setListenerPosition(worldPosition(ctx, entity));
    }

    // Voices whose source went away: the entity was destroyed, the component removed, or playing turned off.
    for (auto it = voices.begin(); it != voices.end();) {
        const AudioSource* source =
            ctx.isAlive(it->first) ? ctx.tryGet<const AudioSource>(it->first) : nullptr;
        if (source == nullptr || !source->playing) {
            engine.stop(it->second);
            it = voices.erase(it);
        } else {
            ++it;
        }
    }

    for (auto [entity, source] : ctx.view<const AudioSource>()) {
        if (!source.playing || !source.clip.isReady()) {
            continue;
        }
        const auto found = voices.find(entity);
        if (found == voices.end()) {
            const audio::VoiceHandle voice =
                engine.play(source.clip.get(), {.volume = source.volume,
                                                .pitch = source.pitch,
                                                .loop = source.loop,
                                                .bus = source.bus,
                                                .spatial = source.spatial,
                                                .position = worldPosition(ctx, entity),
                                                .minDistance = source.minDistance,
                                                .maxDistance = source.maxDistance});
            // Remembered even when it did not start (every voice busy, an empty clip), so it is not retried
            // every frame; playing off and on retries it.
            voices.emplace(entity, voice);
            continue;
        }
        const audio::VoiceHandle voice = found->second;
        engine.setVolume(voice, source.volume);
        engine.setPitch(voice, source.pitch);
        engine.setDistances(voice, source.minDistance, source.maxDistance);
        if (source.spatial) {
            engine.setPosition(voice, worldPosition(ctx, entity));
        }
    }
}

void stopAudioSources(ecs::World& world) {
    auto* sources = world.tryResource<AudioSourceVoices>();
    const auto* audioResource = world.tryResource<audio::Audio>();
    if (sources == nullptr) {
        return;
    }
    if (audioResource != nullptr && audioResource->engine != nullptr) {
        for (const auto& [entity, voice] : sources->voices) {
            audioResource->engine->stop(voice);
        }
    }
    sources->voices.clear();
}

} // namespace teserra::scene
