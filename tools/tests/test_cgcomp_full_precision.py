"""Tests for the fragment assembly rewrite that removes half-precision registers."""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import cgcomp_full_precision as tool  # noqa: E402

SAMPLE = """!!ARBfp1.0
OPTION NV_fragment_program2;
# a comment
PARAM c[1] = { { 1, 2, 0 } };
TEMP R0;
SHORT TEMP H0;
SHORT TEMP H1;
TEMP RC;
TEMP HC;
OUTPUT oCol = result.color;
SLTH  H1.xy, fragment.texcoord[0].x, c[0].yxzw;
MOVXC RC.x, H1.y;
MOVH  oCol(EQ.x), H0;
MULXC HC.x, H0, H1;
MOVR  R0, H0;
MOVX  H0.y(NE.x), c[0].z;
TEX   R0, fragment.texcoord[0], texture[0], 2D;
END
"""


class RewriteTests(unittest.TestCase):
    def setUp(self):
        self.result = tool.to_full_precision(SAMPLE)

    def test_no_half_registers_remain(self):
        self.assertNotRegex(self.result, r"\bH\d+\b")
        self.assertNotIn("SHORT", self.result)

    def test_no_temporary_uses_r0_because_it_is_the_colour_output(self):
        self.assertNotRegex(self.result, r"\bR0\b")

    def test_full_registers_move_up_one(self):
        self.assertIn("TEMP R1;", self.result)

    def test_half_registers_get_their_own_full_registers(self):
        self.assertIn("TEMP R2;", self.result)
        self.assertIn("TEMP R3;", self.result)
        self.assertIn("SLTR  R3.xy,", self.result)

    def test_moved_registers_stay_distinct_inside_one_instruction(self):
        self.assertIn("MOVR  R1, R2;", self.result)

    def test_fixed_and_half_opcodes_become_float(self):
        self.assertIn("MOVRC RC.x, R3.y;", self.result)
        self.assertIn("MULRC RC.x, R2, R3;", self.result)
        self.assertIn("MOVR  R2.y(NE.x), c[0].z;", self.result)

    def test_condition_register_is_single(self):
        self.assertNotIn("HC", self.result)

    def test_texture_opcode_is_unchanged(self):
        self.assertIn("TEX   R1,", self.result)

    def test_swizzle_inside_absolute_bars_is_spelled_out(self):
        # cgcomp's parser counts the closing bar as a swizzle character, so |R1.y| would
        # read as y,x,x,x and a .z destination would get x.
        result = tool.to_full_precision("MAXR  R1.z, |R1.x|, |R1.y|;\nADDR  R2.y, -|R3.xy|, c[0].x;\n")

        self.assertIn("MAXR  R2.z, |R2.xxxx|, |R2.yyyy|;", result)
        self.assertIn("ADDR  R3.y, -|R4.xyyy|, c[0].x;", result)

    def test_absolute_input_register_keeps_its_index(self):
        result = tool.to_full_precision("MAXR  R1.z, |fragment.texcoord[3].y|, |c[2].w|;\n")

        self.assertIn("|fragment.texcoord[3].yyyy|, |c[2].wwww|", result)

    def test_absolute_without_swizzle_is_unchanged(self):
        self.assertIn("-|R2|,", tool.to_full_precision("ADDR  R1.y, -|R1|, c[0];\n"))

    def test_vertex_programs_only_get_the_swizzle_rewrite(self):
        result = tool._rewrite_vertex_program("# |R0.z|\nMAX   R0.x, |R0.z|, |R0.w|;\n")

        self.assertEqual(result, "# |R0.z|\nMAX   R0.x, |R0.zzzz|, |R0.wwww|;\n")

    def test_keeps_header_and_end(self):
        self.assertTrue(self.result.startswith("!!ARBfp1.0"))
        self.assertIn("END", self.result)


if __name__ == "__main__":
    unittest.main(verbosity=2)
