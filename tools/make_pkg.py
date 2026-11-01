"""Make an installable PS3 PKG of Balatro from your own Balatro.exe.

Your game files stay on your PC and go only into your PKG. Do not share the PKG.

Usage: make_pkg.py <path to Balatro.exe> [<output folder>]
"""

import os
import shutil
import subprocess
import sys
import tempfile
import zipfile

import make_game_data
import shader_build

TESTED_VERSION = "1.0.1o-FULL"
CONTENT_ID = "UP0001-BLTR00001_00-0000000000000000"
PKG_NAME = "balatro-ps3.pkg"
TOOL_FOLDER = os.path.dirname(os.path.abspath(__file__))
RUNTIME_FOLDER = os.path.join(TOOL_FOLDER, "..", "runtime")


class NotTheGame(Exception):
    """Raised when the given file is not a copy of Balatro."""


def game_version(game_path: str) -> str:
    try:
        with zipfile.ZipFile(game_path) as archive:
            names = archive.namelist()
            if "main.lua" not in names or "version.jkr" not in names:
                raise NotTheGame(f"{game_path} does not hold the game.")
            return archive.read("version.jkr").decode("utf8").splitlines()[0].strip()
    except (OSError, zipfile.BadZipFile) as error:
        raise NotTheGame(f"{game_path} is not a Balatro.exe that this tool can read.") from error


def version_warning(version: str) -> str | None:
    if version == TESTED_VERSION:
        return None
    return f"WARNING: your game is version {version}. This tool was tested with {TESTED_VERSION}. It may not work."


def make_pkg(game_path: str, output_folder: str) -> str:
    os.environ["PATH"] = TOOL_FOLDER + os.pathsep + os.environ["PATH"]
    output_path = os.path.join(output_folder, PKG_NAME)

    with tempfile.TemporaryDirectory() as work_folder:
        stage = os.path.join(work_folder, "stage")
        game_folder = os.path.join(stage, "USRDIR")

        print("Copying the runtime...", flush=True)
        _copy_runtime(stage, game_folder)
        print("Converting the game files. This takes a minute or two...", flush=True)
        make_game_data.build(game_path, game_folder)
        print("Compiling the shaders...", flush=True)
        shader_build.build(
            os.path.join(game_folder, "shaders"), [os.path.join(game_folder, "resources", "shaders")]
        )
        print("Packing the PKG...", flush=True)
        _pack(work_folder)
        shutil.move(os.path.join(work_folder, PKG_NAME), output_path)
    return output_path


def _copy_runtime(stage: str, game_folder: str) -> None:
    os.makedirs(game_folder)
    for name in os.listdir(RUNTIME_FOLDER):
        source = os.path.join(RUNTIME_FOLDER, name)
        # Only the two files the XMB reads sit above USRDIR.
        destination = stage if name in ("PARAM.SFO", "ICON0.PNG") else game_folder
        shutil.copy(source, destination)


def _pack(work_folder: str) -> None:
    # Relative paths: they are what the PKG stores, and the full path may hold any character.
    subprocess.check_call(
        ["pkg", "--contentid", CONTENT_ID, "stage/", PKG_NAME], cwd=work_folder, stdout=subprocess.DEVNULL
    )


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2

    game_path = sys.argv[1]
    output_folder = sys.argv[2] if len(sys.argv) > 2 else os.getcwd()
    try:
        warning = version_warning(game_version(game_path))
    except NotTheGame as error:
        print(error)
        return 1
    if warning:
        print(warning)

    print(f"Done: {make_pkg(game_path, output_folder)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
