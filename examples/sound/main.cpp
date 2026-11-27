// Sound (#314): the audio API with sound files. A blip decoded from a WAV file plays on Space, with the pitch
// and the pan the arrows set; a tune streamed from an OGG file loops on the Music bus; a buzz circles the
// listener as a spatial voice, so it moves from ear to ear.
//
//   Space            play the blip
//   Left / Right     pan the blip
//   Up / Down        raise or lower the blip's pitch
//   T                start or stop the tune (streamed: assets/tune.ogg.asset.json says "stream": true)
//   1 / 2            turn the music down / up
//   B                start or stop the circling buzz
//   M                mute or unmute everything (the Master bus)
//   Esc              quit
//
//   teserra_sound                                  until Escape or close
//   teserra_sound --frames 120 --hidden-window     smoke run
//   teserra_sound --no-audio                       silent
//
// The sounds are assets/blip.wav and assets/tune.ogg next to the executable, made by
// tools/scripts/make_audio_fixtures.py. The state goes to the log and to the debug UI's "Audio" window
// (backtick).

#include <teserra/assets/asset_registry.hpp>
#include <teserra/audio/audio_clip.hpp>
#include <teserra/audio/audio_engine.hpp>
#include <teserra/core/log.hpp>
#include <teserra/core/math.hpp>
#include <teserra/core/types.hpp>
#include <teserra/engine/app.hpp>
#include <teserra/platform/input.hpp>
#include <teserra/platform/key.hpp>
#if TESERRA_WITH_DEBUG_UI
#include <teserra/debug_ui/debug_ui.hpp>

#include <imgui.h>
#endif

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

using namespace teserra;

namespace {

// The circling buzz: a looping sawtooth, made in code.
audio::AudioClip makeBuzz() {
    constexpr u32 kRate = 22'050;
    std::vector<f32> samples(kRate / 2); // half a second, looped
    for (usize i = 0; i < samples.size(); ++i) {
        const f32 phase = std::fmod(static_cast<f32>(i) * 110.0F / static_cast<f32>(kRate), 1.0F);
        samples[i] = 0.15F * ((2.0F * phase) - 1.0F);
    }
    return *audio::AudioClip::create(kRate, 1, samples);
}

struct State {
    f32 pan = 0.0F;
    f32 pitch = 1.0F;
    audio::VoiceHandle tune;
    audio::VoiceHandle buzz;
    f32 angle = 0.0F;
};

} // namespace

int main(int argc, char** argv) { // NOLINT(bugprone-exception-escape)
    engine::App app{{.title = "Teserra sound", .windowSize = {960, 540}, .args = {argc, argv}}};
    if (!app.isInitialized()) {
        return 1;
    }
    audio::AudioEngine& engine = app.audio();
    const assets::Asset<audio::AudioClip> blip = app.assets().load<audio::AudioClip>("assets://blip.wav");
    const assets::Asset<audio::AudioClip> tune = app.assets().load<audio::AudioClip>("assets://tune.ogg");
    const audio::AudioClip buzz = makeBuzz();
    TESERRA_LOG_INFO(LogCategory::Game, "blip.wav: {:.2f} s, decoded; tune.ogg: {}",
                     blip.get().durationSeconds(), tune.get().isStreamed() ? "streamed" : "decoded");
    TESERRA_LOG_INFO(LogCategory::Game, "Space: blip, arrows: pan and pitch, T: tune, 1/2: music volume, "
                                        "B: buzz, M: mute");

    State state;
#if TESERRA_WITH_DEBUG_UI
    app.debugUi().addWindow("Audio", [&] {
        ImGui::Text("Output: %s",
                    engine.output() == audio::Output::Device ? "sound device" : "none (silent)");
        ImGui::Text("Voices: %u of %u", engine.voiceCount(), engine.maxVoices());
        ImGui::Text("Blip: pan %+.1f, pitch %.2f", static_cast<double>(state.pan),
                    static_cast<double>(state.pitch));
        ImGui::Text("Tune: %s", engine.isPlaying(state.tune) ? "playing (streamed)" : "stopped");
        ImGui::Text("Buzz: %s", engine.isPlaying(state.buzz) ? "circling" : "stopped");
        for (const audio::Bus bus : {audio::Bus::Master, audio::Bus::Music, audio::Bus::Effects}) {
            const std::string_view name = audio::busName(bus);
            ImGui::Text("%.*s: %.0f%%%s", static_cast<int>(name.size()), name.data(),
                        static_cast<double>(engine.busVolume(bus) * 100.0F),
                        engine.isBusMuted(bus) ? " (muted)" : "");
        }
    });
#endif

    const auto update = [&](engine::App& self, f32 deltaSeconds) {
        const platform::Input& input = self.input();
        if (input.wasPressed(platform::Key::Escape)) {
            self.quit();
        }
        if (input.wasPressed(platform::Key::Left) || input.wasPressed(platform::Key::Right)) {
            state.pan =
                std::clamp(state.pan + (input.wasPressed(platform::Key::Left) ? -0.5F : 0.5F), -1.0F, 1.0F);
            TESERRA_LOG_INFO(LogCategory::Game, "Blip pan {:+.1f}", state.pan);
        }
        if (input.wasPressed(platform::Key::Up) || input.wasPressed(platform::Key::Down)) {
            state.pitch =
                std::clamp(state.pitch * (input.wasPressed(platform::Key::Up) ? 1.25F : 0.8F), 0.25F, 4.0F);
            TESERRA_LOG_INFO(LogCategory::Game, "Blip pitch {:.2f}", state.pitch);
        }
        if (input.wasPressed(platform::Key::Space)) {
            engine.play(blip.get(), {.pitch = state.pitch, .pan = state.pan});
        }
        if (input.wasPressed(platform::Key::T)) {
            if (engine.isPlaying(state.tune)) {
                engine.stop(state.tune);
            } else {
                state.tune =
                    engine.play(tune.get(), {.volume = 0.7F, .loop = true, .bus = audio::Bus::Music});
            }
        }
        if (input.wasPressed(platform::Key::Digit1) || input.wasPressed(platform::Key::Digit2)) {
            const f32 step = input.wasPressed(platform::Key::Digit1) ? -0.1F : 0.1F;
            engine.setBusVolume(audio::Bus::Music,
                                std::clamp(engine.busVolume(audio::Bus::Music) + step, 0.0F, 1.0F));
            TESERRA_LOG_INFO(LogCategory::Game, "Music at {:.0f}%",
                             engine.busVolume(audio::Bus::Music) * 100.0F);
        }
        if (input.wasPressed(platform::Key::M)) {
            engine.setBusMuted(audio::Bus::Master, !engine.isBusMuted(audio::Bus::Master));
        }
        if (input.wasPressed(platform::Key::B)) {
            if (engine.isPlaying(state.buzz)) {
                engine.stop(state.buzz);
            } else {
                state.buzz = engine.play(
                    buzz, {.loop = true, .spatial = true, .minDistance = 1.0F, .maxDistance = 12.0F});
            }
        }
        // The buzz circles the listener at the origin once every four seconds, 4 units away.
        state.angle += deltaSeconds * (2.0F * std::numbers::pi_v<float> / 4.0F);
        engine.setPosition(state.buzz, {4.0F * std::cos(state.angle), 4.0F * std::sin(state.angle), 0.0F});
    };

    // The tune starts with the program.
    state.tune = engine.play(tune.get(), {.volume = 0.7F, .loop = true, .bus = audio::Bus::Music});
    const int exitCode = app.run({.update = update});
    log::flush();
    return exitCode;
}
