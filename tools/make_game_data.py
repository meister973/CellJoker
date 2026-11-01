"""Build the game data folder for a PS3 from your own copy of Balatro.

The PKG holds the runtime only, because the game's files are not ours to redistribute. This
reads them from your Balatro.exe and writes a folder to copy over to the console.

Needs Python 3 and Pillow (pip install pillow).

Usage: make_game_data.py <path to Balatro.exe> <output folder>
"""

import io
import os
import sys
import zipfile

from PIL import Image

import texture_cook

LANGUAGES_WITH_LARGE_FONTS = ("ja", "ko", "zh_CN", "zh_TW")
SHIPPED_FONTS = ("resources/fonts/m6x11plus.ttf", "resources/fonts/NotoSans-Bold.ttf")
TEXTURE_PREFIX = "resources/textures/"


def is_needed(name: str) -> bool:
    """The other fonts total 63 MB of main RAM, so the languages that need them do not ship."""
    if name.endswith("/"):
        return False
    if name.startswith("localization/"):
        return name not in [f"localization/{language}.lua" for language in LANGUAGES_WITH_LARGE_FONTS]
    if name.startswith("resources/fonts/"):
        return name in SHIPPED_FONTS
    return True


def output_path(name: str) -> str:
    """Textures are read as cooked/<path>.rtex, in the layout the RSX samples directly."""
    if name.startswith(TEXTURE_PREFIX) and name.lower().endswith(".png"):
        return "cooked/" + name[:-4] + ".rtex"
    return name


def convert(name: str, contents: bytes) -> bytes:
    if name.startswith(TEXTURE_PREFIX) and name.lower().endswith(".png"):
        with Image.open(io.BytesIO(contents)) as image:
            image.load()
            return texture_cook.cook_from_image(image).to_bytes()
    return contents


def build(game_path: str, output_root: str) -> int:
    written = 0
    with zipfile.ZipFile(game_path) as archive:
        for name in archive.namelist():
            if not is_needed(name):
                continue
            destination = os.path.join(output_root, *output_path(name).split("/"))
            os.makedirs(os.path.dirname(destination), exist_ok=True)
            with open(destination, "wb") as handle:
                handle.write(convert(name, archive.read(name)))
            written += 1
    return written


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    if not os.path.isfile(sys.argv[1]):
        print(f"not found: {sys.argv[1]}")
        return 1
    print(f"wrote {build(sys.argv[1], sys.argv[2])} files to {sys.argv[2]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
