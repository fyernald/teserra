#!/usr/bin/env python3
"""Write the audio test fixtures and the sound example's sounds: original tones, so their samples are known.

    pip install soundfile numpy
    python tools/scripts/make_audio_fixtures.py

- engine/audio/tests/data/tone.ogg: OGG Vorbis, stereo, 44.1 kHz, 0.5 s of a 440 Hz sine, 0.5 on the left and
  0.25 on the right.
- examples/sound/assets/blip.wav: 16-bit mono, 22.05 kHz, a 0.2 s square blip gliding from 880 Hz to 440 Hz.
- examples/sound/assets/tune.ogg: OGG Vorbis, mono, 22.05 kHz, an 8-second chiptune loop (A minor, F, C, G).

The test WAV files are written by the tests themselves (engine/audio/tests/wav.hpp), which lets them cut and
corrupt files byte by byte. These files are committed: an encoder is not part of the build. Vorbis encoders
differ between versions, so running the script again may change the OGG files' bytes but not their sound.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
import soundfile

REPO_ROOT = Path(__file__).resolve().parents[2]


def tone() -> tuple[np.ndarray, int]:
    rate = 44_100
    t = np.arange(rate // 2) / rate
    wave = np.sin(2.0 * np.pi * 440.0 * t)
    return np.stack([0.5 * wave, 0.25 * wave], axis=1).astype(np.float32), rate


def note(semitones: int) -> float:
    return 440.0 * 2.0 ** (semitones / 12.0)


def chip(rate: int, length: float, start_hz: float, end_hz: float, volume: float, square: bool) -> np.ndarray:
    """A tone gliding from start_hz to end_hz with a short attack and a decay to silence."""
    count = int(length * rate)
    t = np.arange(count) / count
    frequency = start_hz + (end_hz - start_hz) * t
    phase = np.cumsum(frequency / rate) % 1.0
    wave = np.where(phase < 0.5, 1.0, -1.0) if square else np.where(phase < 0.5, 4.0 * phase - 1.0, 3.0 - 4.0 * phase)
    attack = np.minimum(1.0, np.arange(count) / (0.005 * rate))
    return volume * attack * (1.0 - t) ** 2 * wave


def blip() -> tuple[np.ndarray, int]:
    rate = 22_050
    return chip(rate, 0.2, 880.0, 440.0, 0.4, square=True).astype(np.float32), rate


def tune() -> tuple[np.ndarray, int]:
    rate = 22_050
    eighth = 0.25
    roots = [-24, -28, -21, -26]  # A2 F2 C3 G2
    chords = [[0, 3, 7, 12], [-4, 0, 3, 8], [3, 7, 10, 15], [-2, 2, 5, 10]]
    samples = np.zeros(int(8.0 * rate))
    for bar in range(4):
        for step in range(8):
            start = int(((bar * 8) + step) * eighth * rate)
            bass = chip(rate, eighth, note(roots[bar]), note(roots[bar]), 0.3, square=False)
            lead_note = note(chords[bar][(step * 3) % 4] + 12)
            lead = chip(rate, eighth * 0.9, lead_note, lead_note, 0.08, square=True)
            samples[start:start + len(bass)] += bass
            samples[start:start + len(lead)] += lead
    return samples.astype(np.float32), rate


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", type=Path, default=REPO_ROOT, help="the repository to write into")
    args = parser.parse_args(argv)
    outputs = [
        ("engine/audio/tests/data/tone.ogg", tone(), "OGG", "VORBIS"),
        ("examples/sound/assets/blip.wav", blip(), "WAV", "PCM_16"),
        ("examples/sound/assets/tune.ogg", tune(), "OGG", "VORBIS"),
    ]
    for path, (samples, rate), container, subtype in outputs:
        target = args.root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        soundfile.write(target, samples, rate, format=container, subtype=subtype)
        print(f"wrote {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
