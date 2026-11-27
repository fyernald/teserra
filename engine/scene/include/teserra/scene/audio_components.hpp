#pragma once
// Sounds that belong to entities: AudioSource plays a clip while the entity has it, positioned relative to
// the AudioListener (docs/architecture/audio.md#scene-components). Built with the audio module
// (TESERRA_HAS_AUDIO).

#include <teserra/assets/asset_registry.hpp>
#include <teserra/audio/audio_clip.hpp>
#include <teserra/audio/audio_engine.hpp>
#include <teserra/core/types.hpp>
#include <teserra/ecs/entity.hpp>

#include <string_view>
#include <unordered_map>

namespace teserra::ecs {
class SystemContext;
class World;
} // namespace teserra::ecs

namespace teserra::scene {

/// Plays `clip` while the entity has this component and `playing` is true: the music of a level, a looping
/// waterfall, an enemy's buzz. One-shot sounds are AudioEngine::play() calls in systems instead.
///
///     level.add<scene::AudioSource>(waterfall, {.clip = assets.load<audio::AudioClip>("assets://water.ogg"),
///                                               .loop = true, .spatial = true, .maxDistance = 12.0F});
///
/// The audio system (PostUpdate, after the transforms) starts the voice, applies `volume` and `pitch` each
/// frame, and stops it when the component or the entity goes away, `playing` turns false, or the scene exits
/// or resets. A voice that ended (a clip it does not loop) stays ended until `playing` turns false and true
/// again. The component holds the clip, so it stays loaded while the entity has it.
struct AudioSource {
    assets::Asset<audio::AudioClip> clip;
    audio::Bus bus = audio::Bus::Effects;
    f32 volume = 1.0F;
    /// Greater than 0.
    f32 pitch = 1.0F;
    bool loop = false;
    /// The voice plays while true; false stops it, true again starts it over.
    bool playing = true;
    /// Positional: panned and attenuated by the entity's world position relative to the AudioListener. The
    /// clip, the bus, `loop` and `spatial` apply when the voice starts.
    bool spatial = false;
    /// Full volume up to minDistance world units from the listener, silent from maxDistance on.
    f32 minDistance = 1.0F;
    f32 maxDistance = 20.0F;
};

/// The entity the player hears from, usually the camera: spatial sources are positioned relative to its world
/// position. The first one found is used; without one, the listener stays where it last was (the origin at
/// first).
struct AudioListener {};

/// The name of the audio system in PostUpdate.
inline constexpr std::string_view kAudioSystemName = "scene.audio";

/// The voices of a scene's AudioSources, a resource of every scene's world.
struct AudioSourceVoices {
    std::unordered_map<ecs::Entity, audio::VoiceHandle> voices;
};

/// Starts, updates and stops the voices of AudioSources, and moves the listener. Uses the audio::Audio
/// resource the App inserts; without it (a scene the App has not run), it does nothing. Registered by Scene.
void audioSystem(ecs::SystemContext& ctx);

/// Stops every voice of `world`'s AudioSources: when its scene exits or resets. Sources that are still
/// playing start over the next time the audio system runs.
void stopAudioSources(ecs::World& world);

} // namespace teserra::scene
