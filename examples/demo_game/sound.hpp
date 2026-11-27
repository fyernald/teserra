#pragma once
// The demo game's sounds (#314): synthesized in code at startup, like its art, so the game ships no sound
// files. Effects play on the Effects bus from the gameplay systems; the tune loops on the Music bus as an
// AudioSource of the menu's and the level's scenes.

#include <teserra/assets/asset_registry.hpp>
#include <teserra/audio/audio_clip.hpp>

namespace demo {

/// The game's clips. A resource of the level's and the win screen's scenes, so their systems play them.
struct Sounds {
    teserra::assets::Asset<teserra::audio::AudioClip> coin;
    /// A spike or a slime sends the hero back.
    teserra::assets::Asset<teserra::audio::AudioClip> hurt;
    /// The last coin opens the door.
    teserra::assets::Asset<teserra::audio::AudioClip> door;
    teserra::assets::Asset<teserra::audio::AudioClip> win;
    /// An 8-second loop.
    teserra::assets::Asset<teserra::audio::AudioClip> tune;
};

/// Synthesizes the clips into `assets` (paths "game://sounds/...").
[[nodiscard]] Sounds makeSounds(teserra::assets::AssetRegistry& assets);

} // namespace demo
