"""Zip dist/make_pkg for release. It names what ships, so a game copy or PKG left there stays out.

Usage: release_zip.py <folder holding make_pkg>
"""

import os
import sys
import zipfile

SHIPPED_FILES = ("make_pkg.bat", "HOW_TO_USE.txt", "NOTICES.txt")
SHIPPED_FOLDERS = ("python", "runtime", "tool")


def shipped_paths(tool_folder: str) -> list:
    paths = [os.path.join(tool_folder, name) for name in SHIPPED_FILES]
    for folder in SHIPPED_FOLDERS:
        for parent, children, names in os.walk(os.path.join(tool_folder, folder)):
            children[:] = [child for child in children if child != "__pycache__"]
            paths.extend(os.path.join(parent, name) for name in names)
    return paths


def main() -> int:
    root = sys.argv[1]
    tool_folder = os.path.join(root, "make_pkg")
    with zipfile.ZipFile(tool_folder + ".zip", "w", zipfile.ZIP_DEFLATED) as archive:
        for path in shipped_paths(tool_folder):
            archive.write(path, os.path.relpath(path, root))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
