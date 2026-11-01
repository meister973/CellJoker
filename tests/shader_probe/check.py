"""Compare a probe frame with the values tests/shader_probe/scene.lua sent."""

import sys

from PIL import Image

TOLERANCE = 3

EXPECTED = {
    "a.rgb": ((128, 300), (0.1, 0.2, 0.3)),
    "a.a b.x b.y": ((384, 300), (0.4, 0.5, 0.6)),
    "c d": ((640, 300), (0.7, 0.8, 0.0)),
    "vertex colour": ((896, 300), (0.25, 0.5, 0.75)),
    "screen position and size": ((1152, 360), (1152 / 1280, 360 / 720, 0.5)),
    # math.atan2(0.6, 0.5) / 1.6 and math.hypot(0.5, 0.6). A swizzle lost inside |...| gives
    # pi/4 / 1.6 for the first value.
    "atan uses both arguments": ((1152, 50), (0.8760581 / 1.6, 0.7810250, 0.0)),
    "base red": ((640, 650), (1.0, 0.0, 0.0)),
    "vertex alpha blends": ((150, 550), (0.5, 0.0, 0.5)),
    "shader alpha blends": ((450, 550), (0.5, 0.5, 0.0)),
    "texture alpha blends": ((750, 550), (1.0, 0.5, 0.5)),
    "quad texture alpha blends": ((1050, 550), (1.0, 0.5, 0.5)),
}


def main() -> int:
    image = Image.open(sys.argv[1]).convert("RGB")
    failures = 0

    for label, (position, expected) in EXPECTED.items():
        actual = image.getpixel(position)
        wanted = tuple(round(value * 255) for value in expected)
        matches = all(abs(a - w) <= TOLERANCE for a, w in zip(actual, wanted))
        print(f"{'ok  ' if matches else 'FAIL'} {label}: want {wanted} got {actual}")
        failures += not matches

    image.save(sys.argv[1].replace(".ppm", ".png"))
    print(f"failures={failures}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
