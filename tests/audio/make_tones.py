"""Writes the test sounds: a short and a long 440 Hz tone, at the game's 44.1 kHz rate."""

import os
import sys

import numpy
import soundfile

RATE = 44100
FREQUENCY = 440.0
AMPLITUDE = 0.5
WRITE_FRAMES = 4096


def write_tone(path: str, seconds: float) -> None:
    times = numpy.arange(int(RATE * seconds)) / RATE
    wave = (AMPLITUDE * numpy.sin(2.0 * numpy.pi * FREQUENCY * times)).astype("float32")
    frames = numpy.column_stack((wave, wave))
    # One large write crashes libsndfile's Vorbis encoder, so this writes in small pieces.
    with soundfile.SoundFile(path, "w", RATE, 2, format="OGG", subtype="VORBIS") as file:
        for start in range(0, len(frames), WRITE_FRAMES):
            file.write(frames[start:start + WRITE_FRAMES])


def main() -> int:
    output = sys.argv[1]
    os.makedirs(output, exist_ok=True)
    # The scene opens the long one as a stream, and the short one is decoded whole.
    write_tone(os.path.join(output, "tone_short.ogg"), 1.0)
    write_tone(os.path.join(output, "tone_long.ogg"), 20.0)
    for name in ("tone_short.ogg", "tone_long.ogg"):
        print(name, os.path.getsize(os.path.join(output, name)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
