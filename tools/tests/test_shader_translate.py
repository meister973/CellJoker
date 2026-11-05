"""Tests for the LÖVE GLSL to Cg translator."""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import shader_translate as translate  # noqa: E402

BLUR = """
extern number strength;

vec4 effect( vec4 colour, Image texture, vec2 texture_coords, vec2 screen_coords )
{
    int reach = 1;
    number total = 0.;
    for (int i = -reach; i <= reach; ++i){
        for (int j = -reach; j <= reach; ++j){
            total += Texel( texture, texture_coords + strength*vec2(float(i), float(j))).a;
        }
    }
    return vec4(total);
}
"""


class UnrollTests(unittest.TestCase):
    def test_unrolls_a_symmetric_loop_into_one_block_per_step(self):
        code = translate.unroll_loops("for (int i = -1; i <= 1; ++i){ sum += i; }")

        self.assertEqual(code, "{ sum += (-1); }{ sum += (0); }{ sum += (1); }")

    def test_reads_the_bound_from_a_local_constant(self):
        code = translate.unroll_loops("int reach = 2; for (int k = -reach; k <= reach; ++k){ s += k; }")

        self.assertEqual(code.count("s +="), 5)
        self.assertNotIn("for", code)

    def test_unrolls_nested_loops(self):
        code = translate.unroll_loops(
            "for (int i = -1; i <= 1; ++i){ for (int j = -1; j <= 1; ++j){ s += i * j; } }"
        )

        self.assertEqual(code.count("s +="), 9)
        self.assertIn("s += (-1) * (1);", code)


class HologramTests(unittest.TestCase):
    def test_the_hologram_shader_has_no_loop_left(self):
        shader = translate.translate("hologram", BLUR)

        self.assertNotIn("for (", shader.fragment)
        self.assertEqual(shader.fragment.count("tex2D( love_image"), 9)

    def test_other_shaders_keep_their_loops(self):
        shader = translate.translate("blur", BLUR)

        self.assertIn("for (", shader.fragment)


if __name__ == "__main__":
    unittest.main(verbosity=2)
