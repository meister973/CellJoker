"""Build the folder of shader programs the game loads: <name>.vpo, <name>.fpo, and table.txt.

Needs cgcomp on the PATH, with NVIDIA's Cg library where cgcomp can load it.

Usage: shader_build.py <output folder> <shader folder> [<shader folder> ...]
"""

import os
import shutil
import sys
import tempfile

import cgcomp_full_precision
import shader_translate

# (stage flag, source extension, program extension, field of TranslatedShader)
STAGES = (("-v", ".vcg", ".vpo", "vertex"), ("-f", ".fcg", ".fpo", "fragment"))


class ShaderBuildError(Exception):
    """Raised when cgcomp cannot compile a shader."""


def build(output_folder: str, source_folders: list) -> int:
    os.makedirs(output_folder, exist_ok=True)
    shaders = shader_translate.translate_folders(source_folders)

    with tempfile.TemporaryDirectory() as work_folder:
        for shader in shaders:
            _compile_shader(shader, work_folder, output_folder)

    with open(os.path.join(output_folder, "table.txt"), "w", encoding="utf8", newline="\n") as handle:
        handle.write(shader_translate.shader_table(shaders))
    return len(shaders)


def _compile_shader(shader, work_folder: str, output_folder: str) -> None:
    for stage, source_extension, program_extension, source_field in STAGES:
        source_name = shader.name + source_extension
        program_name = shader.name + program_extension
        with open(os.path.join(work_folder, source_name), "w", encoding="utf8", newline="\n") as handle:
            handle.write(getattr(shader, source_field))

        status = cgcomp_full_precision.compile_program(stage, source_name, program_name, work_folder)
        if status != 0:
            raise ShaderBuildError(f"cgcomp failed on {source_name}: exit {status}")
        shutil.copyfile(os.path.join(work_folder, program_name), os.path.join(output_folder, program_name))


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    print(f"built {build(sys.argv[1], sys.argv[2:])} shaders in {sys.argv[1]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
