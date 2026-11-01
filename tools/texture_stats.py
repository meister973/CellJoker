"""Report cooked texture cost against the console's memory budget.

The RSX has 256 MB of GDDR3 but only part of it is usable for textures once
framebuffers, depth and the fragment programs are resident. This prints what the
art actually costs when decoded with full mip chains, so the budget question is
answered with a measurement rather than a guess.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import texture_cook as cook  # noqa: E402

MEGABYTE = 1024 * 1024
RSX_BUDGET_MB = 200


def collect(root):
    results = []
    for dirpath, _, files in os.walk(root):
        for name in files:
            if not name.lower().endswith(".png"):
                continue
            path = os.path.join(dirpath, name)
            texture = cook.cook(path, mipmaps=True)
            cooked = sum(len(level) for level in texture.levels)
            results.append((cooked, texture.width, texture.height,
                            os.path.relpath(path, root)))
    results.sort(reverse=True)
    return results


def main():
    if len(sys.argv) != 2:
        print("usage: texture_stats.py <texture-directory>")
        return 2

    root = sys.argv[1]
    results = collect(root)
    if not results:
        print(f"no png files under {root}")
        return 1

    source_bytes = sum(
        os.path.getsize(os.path.join(root, rel)) for _, _, _, rel in results
    )
    cooked_bytes = sum(size for size, _, _, _ in results)

    print(f"textures      : {len(results)}")
    print(f"png on disk   : {source_bytes / MEGABYTE:.1f} MB")
    print(f"cooked + mips : {cooked_bytes / MEGABYTE:.1f} MB")
    print(f"growth        : {cooked_bytes / source_bytes:.2f}x")
    print(f"rsx budget    : {RSX_BUDGET_MB} MB -> {cooked_bytes / MEGABYTE / RSX_BUDGET_MB * 100:.0f}% used")
    print()
    print("largest:")

    for size, width, height, rel in results[:10]:
        print(f"  {size / MEGABYTE:7.2f} MB  {width:4}x{height:<4}  {rel}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
