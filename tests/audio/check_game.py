"""Measures the WAV from a run of the real game: music must play, and must not click or clip."""

import sys

import numpy

SAMPLE_RATE = 48000
WAV_HEADER_BYTES = 44
CLIPPED = 0.999
MINIMUM_LEVEL = 0.005
SILENT_SECONDS_LIMIT = 3.0
WINDOW_SECONDS = 0.1


def main() -> int:
    # The run ends by killing RPCS3, which writes the WAV header only on a clean exit.
    audio = numpy.fromfile(sys.argv[1], dtype="<f4", offset=WAV_HEADER_BYTES)
    rate = SAMPLE_RATE
    left = audio[:len(audio) // 2 * 2].reshape(-1, 2)[:, 0]
    # The first part is loading, and the splash screen starts quietly.
    playing = left[int(rate * 20):]
    window = int(rate * WINDOW_SECONDS)

    levels = numpy.array([
        numpy.sqrt(numpy.mean(playing[start:start + window] ** 2))
        for start in range(0, len(playing) - window, window)
    ])
    silent = levels < 0.0005
    longest = 0
    run = 0
    for is_silent in silent:
        run = run + 1 if is_silent else 0
        longest = max(longest, run)

    clipped = float(numpy.mean(numpy.abs(playing) >= CLIPPED))
    level = float(numpy.sqrt(numpy.mean(playing ** 2)))
    longest_seconds = longest * WINDOW_SECONDS

    print(f"seconds={len(left) / rate:.1f} level={level:.4f} peak={numpy.abs(playing).max():.3f} "
          f"clipped={clipped:.5f} longest_silence={longest_seconds:.1f}s")

    good = level >= MINIMUM_LEVEL and clipped < 0.001 and longest_seconds <= SILENT_SECONDS_LIMIT
    print("ok" if good else "FAIL")
    return 0 if good else 1


if __name__ == "__main__":
    raise SystemExit(main())
