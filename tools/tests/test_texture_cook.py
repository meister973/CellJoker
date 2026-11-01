"""Tests for the texture cook.

These are the checks that would otherwise be discovered on hardware as scrambled
or colour-swapped art, which is exactly the failure mode that costs a day.
"""

import os
import struct
import sys
import tempfile
import unittest

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import texture_cook as cook  # noqa: E402

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SAMPLE_TEXTURE_DIR = os.path.join(REPO_ROOT, "game", "resources", "textures")


def make_image(width, height, colour):
    image = Image.new("RGBA", (width, height), colour)
    return image


class CookedHeaderTests(unittest.TestCase):
    def test_header_records_dimensions_and_format(self):
        texture = cook.cook_from_image(make_image(4, 4, (255, 0, 0, 255)), mipmaps=True)

        magic, version, format_id, flags, levels, width, height, _, offset = (
            cook.HEADER.unpack(texture.to_bytes()[: cook.HEADER.size])
        )

        self.assertEqual(magic, b"RTEX")
        self.assertEqual(version, 1)
        self.assertEqual(format_id, cook.FORMAT_A8R8G8B8)
        self.assertEqual(flags, cook.FLAG_LINEAR)
        self.assertEqual((width, height), (4, 4))
        self.assertEqual(levels, 3)
        self.assertEqual(offset, cook.HEADER.size + cook.LEVEL.size * 3)


class TexelOrderTests(unittest.TestCase):
    def test_opaque_red_packs_as_big_endian_argb(self):
        texture = cook.cook_from_image(make_image(1, 1, (255, 0, 0, 255)))

        # 0xAARRGGBB written big-endian is the byte sequence A,R,G,B.
        self.assertEqual(texture.levels[0], b"\xff\xff\x00\x00")

    def test_alpha_is_preserved(self):
        texture = cook.cook_from_image(make_image(1, 1, (0, 0, 0, 128)))

        self.assertEqual(texture.levels[0][0], 128)

    def test_blue_channel_lands_in_the_last_byte(self):
        texture = cook.cook_from_image(make_image(1, 1, (0, 0, 255, 255)))

        self.assertEqual(texture.levels[0], b"\xff\x00\x00\xff")


class MipChainTests(unittest.TestCase):
    def test_cooking_defaults_to_one_level(self):
        texture = cook.cook_from_image(make_image(8, 8, (0, 0, 0, 255)))

        self.assertEqual(len(texture.levels), 1)

    def test_square_chain_halves_to_one_by_one(self):
        texture = cook.cook_from_image(make_image(8, 8, (0, 0, 0, 255)), mipmaps=True)

        self.assertEqual(len(texture.levels), 4)
        self.assertEqual([len(level) for level in texture.levels],
                         [8 * 8 * 4, 4 * 4 * 4, 2 * 2 * 4, 1 * 1 * 4])

    def test_non_power_of_two_stops_at_one(self):
        texture = cook.cook_from_image(make_image(5, 3, (0, 0, 0, 255)), mipmaps=True)

        self.assertEqual(len(texture.levels), 3)
        self.assertEqual(len(texture.levels[-1]), 4)


class ContainerTests(unittest.TestCase):
    def test_level_table_offsets_are_contiguous(self):
        texture = cook.cook_from_image(make_image(8, 8, (10, 20, 30, 40)), mipmaps=True)
        data = texture.to_bytes()

        base = cook.HEADER.size + cook.LEVEL.size * len(texture.levels)
        for index, level in enumerate(texture.levels):
            offset, size = cook.LEVEL.unpack_from(data, cook.HEADER.size + index * cook.LEVEL.size)
            self.assertEqual(size, len(level))
            self.assertEqual(offset, base)
            self.assertEqual(data[offset:offset + size], level)
            base += size

    def test_payload_is_exactly_the_sum_of_levels(self):
        texture = cook.cook_from_image(make_image(16, 16, (1, 2, 3, 4)))
        data = texture.to_bytes()

        expected = cook.HEADER.size + cook.LEVEL.size * len(texture.levels)
        expected += sum(len(level) for level in texture.levels)

        self.assertEqual(len(data), expected)


class CookTreeTests(unittest.TestCase):
    def test_tree_mirrors_png_paths_as_rtex(self):
        with tempfile.TemporaryDirectory() as source, tempfile.TemporaryDirectory() as output:
            os.makedirs(os.path.join(source, "2x"))
            make_image(2, 2, (1, 2, 3, 4)).save(os.path.join(source, "2x", "Jokers.png"))

            count = cook.cook_tree(source, output)

            self.assertEqual(count, 1)
            self.assertTrue(os.path.isfile(os.path.join(output, "2x", "Jokers.rtex")))


class RealAssetTests(unittest.TestCase):
    def test_a_shipped_texture_cooks(self):
        if not os.path.isdir(SAMPLE_TEXTURE_DIR):
            self.skipTest("game textures not staged")

        names = sorted(
            os.path.join(root, name)
            for root, _, files in os.walk(SAMPLE_TEXTURE_DIR)
            for name in files
            if name.lower().endswith(".png")
        )
        if not names:
            self.skipTest("no png files present")

        path = names[0]
        texture = cook.cook(path)

        header = cook.HEADER.unpack(texture.to_bytes()[: cook.HEADER.size])
        self.assertEqual(header[0], b"RTEX")
        self.assertGreater(texture.width, 0)
        self.assertGreater(texture.height, 0)
        self.assertGreaterEqual(len(texture.levels), 1)

    def test_swizzle_is_refused_rather_than_silently_wrong(self):
        with self.assertRaises(cook.TextureCookError):
            cook.cook_from_image(make_image(4, 4, (1, 2, 3, 4)), swizzle=True)

    def test_missing_file_raises_cook_error(self):
        with self.assertRaises(cook.TextureCookError):
            cook.cook(os.path.join(SAMPLE_TEXTURE_DIR, "does-not-exist.png"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
