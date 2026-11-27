#include "sound.hpp"

#include <teserra/core/types.hpp>

#include <array>
#include <cmath>
#include <numbers>
#include <span>
#include <string>
#include <vector>

namespace demo {

using namespace teserra;

namespace {

constexpr u32 kRate = 22'050;

enum class Wave : u8 {
    Square,
    Triangle
};

// The frequency of a note `semitones` from A4 (440 Hz).
f32 note(int semitones) {
    return 440.0F * std::pow(2.0F, static_cast<f32>(semitones) / 12.0F);
}

// One period of `wave` at `phase` in [0, 1), from -1 to 1.
f32 shape(Wave wave, f32 phase) {
    if (wave == Wave::Square) {
        return phase < 0.5F ? 1.0F : -1.0F;
    }
    return phase < 0.5F ? (4.0F * phase) - 1.0F : 3.0F - (4.0F * phase);
}

// Adds a tone to `samples` from `start` seconds for `length` seconds, gliding from `from` to `to` Hz, with a
// short attack and a decay to silence: chiptune effects.
void addTone(std::vector<f32>& samples, Wave wave, f32 start, f32 length, f32 from, f32 to, f32 volume) {
    const auto first = static_cast<usize>(start * kRate);
    const auto count = static_cast<usize>(length * kRate);
    if (samples.size() < first + count) {
        samples.resize(first + count, 0.0F);
    }
    f32 phase = 0.0F;
    for (usize i = 0; i < count; ++i) {
        const f32 t = static_cast<f32>(i) / static_cast<f32>(count);
        const f32 frequency = from + ((to - from) * t);
        phase += frequency / static_cast<f32>(kRate);
        phase -= std::floor(phase);
        const f32 attack = std::min(1.0F, static_cast<f32>(i) / (0.005F * kRate));
        const f32 envelope = attack * (1.0F - t) * (1.0F - t);
        samples[first + i] += volume * envelope * shape(wave, phase);
    }
}

assets::Asset<audio::AudioClip> clip(assets::AssetRegistry& assets, const std::string& name,
                                     const std::vector<f32>& samples) {
    return assets.load<audio::AudioClip>("game://sounds/" + name,
                                         [&] { return audio::AudioClip::create(kRate, 1, samples); });
}

std::vector<f32> coinSound() {
    std::vector<f32> samples;
    addTone(samples, Wave::Square, 0.0F, 0.06F, note(14), note(14), 0.2F);  // B5
    addTone(samples, Wave::Square, 0.05F, 0.16F, note(19), note(19), 0.2F); // E6
    return samples;
}

std::vector<f32> hurtSound() {
    std::vector<f32> samples;
    addTone(samples, Wave::Square, 0.0F, 0.3F, 300.0F, 70.0F, 0.25F);
    return samples;
}

std::vector<f32> doorSound() {
    std::vector<f32> samples;
    const std::array<int, 4> notes{3, 7, 10, 15}; // C5 E5 G5 C6
    for (usize i = 0; i < notes.size(); ++i) {
        addTone(samples, Wave::Triangle, 0.08F * static_cast<f32>(i), 0.25F, note(notes[i]), note(notes[i]),
                0.35F);
    }
    return samples;
}

std::vector<f32> winSound() {
    std::vector<f32> samples;
    const std::array<int, 6> notes{3, 7, 10, 15, 19, 22}; // C5 E5 G5 C6 E6 G6
    for (usize i = 0; i < notes.size(); ++i) {
        const f32 length = i + 1 == notes.size() ? 0.6F : 0.2F;
        addTone(samples, Wave::Square, 0.11F * static_cast<f32>(i), length, note(notes[i]), note(notes[i]),
                0.15F);
        addTone(samples, Wave::Triangle, 0.11F * static_cast<f32>(i), length, note(notes[i] - 12),
                note(notes[i] - 12), 0.25F);
    }
    return samples;
}

// Four bars at 120 beats per minute, A minor, F, C, G: a triangle bass on eighth notes and a soft square
// arpeggio. Exactly 8 seconds, so it loops without a gap.
std::vector<f32> tune() {
    constexpr f32 kEighth = 0.25F;
    // Roots of the chords, in semitones from A4, and their arpeggios.
    const std::array<int, 4> roots{-24, -28, -21, -26};               // A2 F2 C3 G2
    const std::array<std::array<int, 4>, 4> chords{{{0, 3, 7, 12},    // A C E A
                                                    {-4, 0, 3, 8},    // F A C F
                                                    {3, 7, 10, 15},   // C E G C
                                                    {-2, 2, 5, 10}}}; // G B D G
    std::vector<f32> samples(static_cast<usize>(8.0F * kRate), 0.0F);
    for (usize bar = 0; bar < 4; ++bar) {
        for (usize step = 0; step < 8; ++step) {
            const f32 start = (static_cast<f32>((bar * 8) + step)) * kEighth;
            addTone(samples, Wave::Triangle, start, kEighth, note(roots[bar]), note(roots[bar]), 0.3F);
            const int lead = chords[bar][step % 4];
            addTone(samples, Wave::Square, start, kEighth * 0.9F, note(lead), note(lead), 0.06F);
        }
    }
    samples.resize(static_cast<usize>(8.0F * kRate));
    return samples;
}

} // namespace

Sounds makeSounds(assets::AssetRegistry& assets) {
    return {.coin = clip(assets, "coin", coinSound()),
            .hurt = clip(assets, "hurt", hurtSound()),
            .door = clip(assets, "door", doorSound()),
            .win = clip(assets, "win", winSound()),
            .tune = clip(assets, "tune", tune())};
}

} // namespace demo
