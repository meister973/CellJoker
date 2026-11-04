"""Tests for the tool that builds console game data from a tester's own Balatro.exe."""

import io
import os
import sys
import tempfile
import unittest
import zipfile

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import make_game_data as tool  # noqa: E402
import texture_cook  # noqa: E402


def png_bytes():
    buffer = io.BytesIO()
    Image.new("RGBA", (2, 2), (1, 2, 3, 4)).save(buffer, "PNG")
    return buffer.getvalue()


class SelectionTests(unittest.TestCase):
    def test_keeps_lua_and_english(self):
        self.assertTrue(tool.is_needed("engine/ui.lua"))
        self.assertTrue(tool.is_needed("localization/en-us.lua"))

    def test_keeps_every_language(self):
        for language in ("de", "es_419", "es_ES", "fr", "id", "it", "ja", "ko", "nl", "pl", "pt_BR", "ru", "zh_CN", "zh_TW"):
            self.assertTrue(tool.is_needed(f"localization/{language}.lua"), language)

    def test_keeps_every_font(self):
        self.assertTrue(tool.is_needed("resources/fonts/m6x11plus.ttf"))
        self.assertTrue(tool.is_needed("resources/fonts/NotoSans-Bold.ttf"))
        self.assertTrue(tool.is_needed("resources/fonts/NotoSansJP-Bold.ttf"))

    def test_skips_folder_entries(self):
        self.assertFalse(tool.is_needed("resources/fonts/"))


class BuildTests(unittest.TestCase):
    def test_builds_the_layout_the_runtime_reads(self):
        with tempfile.TemporaryDirectory() as folder:
            game = os.path.join(folder, "Balatro.exe")
            # A fused game is an executable with a zip appended, so the prefix must not matter.
            with open(game, "wb") as handle:
                handle.write(b"MZ executable bytes")
            with zipfile.ZipFile(game, "a") as archive:
                archive.writestr("main.lua", "-- game")
                archive.writestr("localization/ja.lua", "-- japanese")
                archive.writestr("resources/textures/2x/Jokers.png", png_bytes())
                archive.writestr("resources/sounds/win.ogg", b"ogg")

            output = os.path.join(folder, "USRDIR")
            written = tool.build(game, output)

            self.assertEqual(written, 4)
            self.assertTrue(os.path.isfile(os.path.join(output, "main.lua")))
            self.assertTrue(os.path.isfile(os.path.join(output, "resources", "sounds", "win.ogg")))
            self.assertTrue(os.path.isfile(os.path.join(output, "localization", "ja.lua")))

            cooked = os.path.join(output, "cooked", "resources", "textures", "2x", "Jokers.rtex")
            with open(cooked, "rb") as handle:
                self.assertEqual(handle.read(4), texture_cook.MAGIC)


if __name__ == "__main__":
    unittest.main(verbosity=2)
