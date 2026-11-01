"""Compare a slots frame with the values tests/shader_probe/slots_scene.lua sent."""

import sys

from PIL import Image

TOLERANCE = 3


def main() -> int:
    image = Image.open(sys.argv[1]).convert("RGB")
    failures = 0

    for slot in range(6):
        base = (slot + 1) / 10
        x = 80 + slot * 160
        wanted_xyz = tuple(round((base + offset) * 255) for offset in (0.0, 0.02, 0.04))
        wanted_w = round((base + 0.06) * 255)
        xyz = image.getpixel((x, 180))
        w = image.getpixel((x, 540))[0]
        matches = all(abs(a - b) <= TOLERANCE for a, b in zip(xyz, wanted_xyz)) and abs(w - wanted_w) <= TOLERANCE
        print(f"{'ok  ' if matches else 'FAIL'} slot {slot} TEXCOORD{slot + 2}: want {wanted_xyz} w={wanted_w} got {xyz} w={w}")
        failures += not matches

    print(f"failures={failures}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
