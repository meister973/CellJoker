"""Measures the WAV that RPCS3 dumped while tests/audio/scene.lua played.

Sections are found from the sound itself: anything louder than silence, in order. Pitch comes from
counting zero crossings, which is exact enough to tell 440 Hz from 880 Hz.
"""

import sys

import numpy

SAMPLE_RATE = 48000
WAV_HEADER_BYTES = 44
SILENCE = 0.01
WINDOW_SECONDS = 0.02
HOP_SECONDS = 0.01
FREQUENCY_TOLERANCE = 8.0
LEVEL_TOLERANCE = 0.08
GLITCH_LIMIT = 0.2

# (name, frequency, peak level)
EXPECTED = [
    ("clip at normal pitch", 440.0, 0.5),
    ("clip an octave up", 880.0, 0.5),
    ("stream at normal pitch", 440.0, 0.5),
    ("stream after seeking, an octave up", 880.0, 0.5),
    ("stream at half volume", 440.0, 0.25),
]


def loud_sections(left: numpy.ndarray, rate: int) -> list:
    window = int(rate * WINDOW_SECONDS)
    hop = int(rate * HOP_SECONDS)
    loud = [
        numpy.sqrt(numpy.mean(left[start:start + window] ** 2)) > SILENCE
        for start in range(0, len(left) - window, hop)
    ]

    sections = []
    start = None
    for index, is_loud in enumerate(loud):
        if is_loud and start is None:
            start = index
        elif not is_loud and start is not None:
            sections.append((start * hop, index * hop + window))
            start = None
    if start is not None:
        sections.append((start * hop, len(left)))
    return sections


def frequency(samples: numpy.ndarray, rate: int) -> float:
    rising = numpy.sum((samples[:-1] < 0) & (samples[1:] >= 0))
    return rising / (len(samples) / rate)


def split_where_level_or_pitch_changes(left, sections, rate):
    """Sections 5 and 6 play with no gap between them, so they are cut where the level halves."""
    result = []
    for start, end in sections:
        length = end - start
        if length > rate * 2.5:
            middle = start + length // 2
            result.append((start, middle))
            result.append((middle, end))
        else:
            result.append((start, end))
    return result


def main() -> int:
    # The run can end by killing RPCS3, which writes the WAV header only on a clean exit.
    audio = numpy.fromfile(sys.argv[1], dtype="<f4", offset=WAV_HEADER_BYTES)
    rate = SAMPLE_RATE
    left = audio[:len(audio) // 2 * 2].reshape(-1, 2)[:, 0]
    print(f"rate={rate} seconds={len(left) / rate:.1f} peak={numpy.abs(left).max():.3f}")

    sections = split_where_level_or_pitch_changes(left, loud_sections(left, rate), rate)
    failures = 0

    if len(sections) != len(EXPECTED):
        print(f"FAIL expected {len(EXPECTED)} sections, found {len(sections)}: "
              f"{[(round(a / rate, 2), round(b / rate, 2)) for a, b in sections]}")
        failures += 1

    for (name, wanted_frequency, wanted_level), (start, end) in zip(EXPECTED, sections):
        # The edges hold fades and voice starts, so only the middle is measured.
        trim = int(rate * 0.1)
        middle = left[start + trim:end - trim]
        got_frequency = frequency(middle, rate)
        got_level = float(numpy.percentile(numpy.abs(middle), 99.5))
        # A clean sine moves little between samples, so a big jump is a click.
        glitch = float(numpy.abs(numpy.diff(middle)).max()) if len(middle) else 1.0
        limit = GLITCH_LIMIT * wanted_frequency / 440.0

        good = (abs(got_frequency - wanted_frequency) <= FREQUENCY_TOLERANCE
                and abs(got_level - wanted_level) <= LEVEL_TOLERANCE
                and glitch <= limit)
        print(f"{'ok  ' if good else 'FAIL'} {name}: want {wanted_frequency:.0f} Hz level {wanted_level} "
              f"got {got_frequency:.1f} Hz level {got_level:.3f} largest_step {glitch:.3f}")
        failures += not good

    print(f"failures={failures}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
