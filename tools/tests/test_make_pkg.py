"""Tests for make_pkg's checks on the file the user drops on it."""

import io
import os
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import make_pkg


def fake_game(version: str, extra_bytes: bytes = b"MZ not really a program") -> str:
    """A stand-in Balatro.exe: a program with the game's zip joined to its end."""
    archive = io.BytesIO()
    with zipfile.ZipFile(archive, "w") as contents:
        contents.writestr("main.lua", "-- game")
        contents.writestr("version.jkr", f"{version}\n1.0.1o\n")
    handle, path = tempfile.mkstemp(suffix=".exe")
    with os.fdopen(handle, "wb") as output:
        output.write(extra_bytes + archive.getvalue())
    return path


class GameCheckTests(unittest.TestCase):
    def test_reads_the_version_of_a_real_game_file(self):
        path = fake_game("1.0.1o-FULL")
        self.addCleanup(os.remove, path)

        self.assertEqual(make_pkg.game_version(path), "1.0.1o-FULL")

    def test_rejects_a_program_that_holds_no_zip(self):
        handle, path = tempfile.mkstemp(suffix=".exe")
        os.close(handle)
        self.addCleanup(os.remove, path)

        with self.assertRaises(make_pkg.NotTheGame):
            make_pkg.game_version(path)

    def test_rejects_a_zip_that_is_not_the_game(self):
        handle, path = tempfile.mkstemp(suffix=".zip")
        os.close(handle)
        self.addCleanup(os.remove, path)
        with zipfile.ZipFile(path, "w") as contents:
            contents.writestr("readme.txt", "hello")

        with self.assertRaises(make_pkg.NotTheGame):
            make_pkg.game_version(path)

    def test_rejects_a_missing_file(self):
        with self.assertRaises(make_pkg.NotTheGame):
            make_pkg.game_version("no such file.exe")


class VersionWarningTests(unittest.TestCase):
    def test_tested_version_gives_no_warning(self):
        self.assertIsNone(make_pkg.version_warning(make_pkg.TESTED_VERSION))

    def test_other_version_names_both_versions(self):
        warning = make_pkg.version_warning("1.1.0-FULL")

        self.assertIn("1.1.0-FULL", warning)
        self.assertIn(make_pkg.TESTED_VERSION, warning)


if __name__ == "__main__":
    unittest.main(verbosity=2)
