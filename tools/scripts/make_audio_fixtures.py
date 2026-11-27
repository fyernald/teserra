#!/usr/bin/env python3
"""Write the audio test fixtures: original tones, so their samples are known exactly.

    pip install soundfile numpy
    python tools/scripts/make_audio_fixtures.py            # writes engine/audio/tests/data/

- tone.ogg: OGG Vorbis, stereo, 44.1 kHz, 0.5 s of a 440 Hz sine, 0.5 on the left and 0.25 on the right.

The WAV fixtures are written by the tests themselves (engine/audio/tests/wav.hpp), which lets them cut and
corrupt files byte by byte. The OGG file is committed: an encoder is not part of the build.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
import soundfile

REPO_ROOT = Path(__file__).resolve().parents[2]
SAMPLE_RATE = 44_100
FRAMES = SAMPLE_RATE // 2
FREQUENCY = 440.0


def tone() -> np.ndarray:
    t = np.arange(FRAMES) / SAMPLE_RATE
    wave = np.sin(2.0 * np.pi * FREQUENCY * t)
    return np.stack([0.5 * wave, 0.25 * wave], axis=1).astype(np.float32)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path, default=REPO_ROOT / "engine/audio/tests/data")
    args = parser.parse_args(argv)
    args.out.mkdir(parents=True, exist_ok=True)
    soundfile.write(args.out / "tone.ogg", tone(), SAMPLE_RATE, format="OGG", subtype="VORBIS")
    print(f"wrote {args.out / 'tone.ogg'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
