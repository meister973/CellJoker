"""Cook PNGs into textures the RSX can consume.

Layout decisions, deliberately conservative for a first pass:

* Linear (pitch-based) rather than swizzled. Swizzling is a Phase 13
  optimisation; getting the correct pixels on screen comes first.
* A8R8G8B8, stored as big-endian words. On a big-endian machine a
  0xAARRGGBB word is the byte sequence A,R,G,B, which is what we write.
* One level by default. The game's atlases are drawn near 1:1, and no test
  shows how RSX expects a linear mip chain to be packed.

Header is big-endian throughout so the console can read it with no swapping.

Usage: texture_cook.py <png-folder> <output-folder>
"""

from __future__ import annotations

import os
import struct
import sys
from dataclasses import dataclass

from PIL import Image

MAGIC = b"RTEX"
VERSION = 1

FORMAT_A8R8G8B8 = 1

FLAG_LINEAR = 0
FLAG_SWIZZLED = 1

HEADER = struct.Struct(">4sHHHHHHHI")
LEVEL = struct.Struct(">II")

BYTES_PER_TEXEL = 4


class TextureCookError(Exception):
    """Raised when an image cannot be cooked."""


@dataclass(frozen=True)
class CookedTexture:
    width: int
    height: int
    format: int
    flags: int
    levels: tuple[bytes, ...]

    def to_bytes(self) -> bytes:
        header_size = HEADER.size + LEVEL.size * len(self.levels)
        table = bytearray()
        payload = bytearray()
        offset = header_size

        for level in self.levels:
            table += LEVEL.pack(offset, len(level))
            payload += level
            offset += len(level)

        header = HEADER.pack(
            MAGIC,
            VERSION,
            self.format,
            self.flags,
            len(self.levels),
            self.width,
            self.height,
            0,
            header_size,
        )
        return header + bytes(table) + bytes(payload)


def generate_mips(image: Image.Image) -> list[Image.Image]:
    """Halve until 1x1. Every level is RGBA so packing stays uniform."""
    mips = []
    level = image.convert("RGBA")

    while True:
        mips.append(level)
        if level.width == 1 and level.height == 1:
            return mips
        size = (max(1, level.width // 2), max(1, level.height // 2))
        level = level.resize(size, Image.Resampling.LANCZOS)


def pack_argb_be(image: Image.Image) -> bytes:
    """RGBA bytes in, A,R,G,B bytes out - a big-endian 0xAARRGGBB word."""
    rgba = image.convert("RGBA").tobytes()
    packed = bytearray(len(rgba))

    packed[0::4] = rgba[3::4]  # alpha
    packed[1::4] = rgba[0::4]  # red
    packed[2::4] = rgba[1::4]  # green
    packed[3::4] = rgba[2::4]  # blue

    return bytes(packed)


def cook_from_image(
    image: Image.Image, swizzle: bool = False, mipmaps: bool = False
) -> CookedTexture:
    if swizzle:
        raise TextureCookError("swizzled layout is not implemented yet (Phase 13)")

    mips = generate_mips(image) if mipmaps else [image.convert("RGBA")]

    return CookedTexture(
        width=mips[0].width,
        height=mips[0].height,
        format=FORMAT_A8R8G8B8,
        flags=FLAG_LINEAR,
        levels=tuple(pack_argb_be(level) for level in mips),
    )


def cook(path: str, swizzle: bool = False, mipmaps: bool = False) -> CookedTexture:
    try:
        with Image.open(path) as source:
            source.load()
            return cook_from_image(source, swizzle=swizzle, mipmaps=mipmaps)
    except OSError as error:
        raise TextureCookError(f"cannot read {path}: {error}") from error


def level_size(width: int, height: int) -> int:
    return width * height * BYTES_PER_TEXEL


def cook_tree(source_root: str, output_root: str) -> int:
    """Mirror a folder of PNGs as .rtex files. Returns how many were written."""
    count = 0

    for folder, _, names in os.walk(source_root):
        relative = os.path.relpath(folder, source_root)
        for name in names:
            stem, extension = os.path.splitext(name)
            if extension.lower() != ".png":
                continue
            destination = os.path.join(output_root, relative, stem + ".rtex")
            os.makedirs(os.path.dirname(destination), exist_ok=True)
            with open(destination, "wb") as handle:
                handle.write(cook(os.path.join(folder, name)).to_bytes())
            count += 1

    return count


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__)
        raise SystemExit(2)
    print(f"cooked {cook_tree(sys.argv[1], sys.argv[2])} textures")
