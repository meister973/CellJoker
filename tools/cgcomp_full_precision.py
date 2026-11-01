"""Stand-in for cgcomp that makes fragment programs correct on RSX.

Cg treats `result.color` and its temporary R0 as separate registers. On RSX they are the
same register, so a program that writes the output and later uses R0 overwrites the
colour. The logo showed this: its shape and blue were right, while red and green held
leftover values. So every temporary moves up one register.

cgcomp also always passes -fastprecision to Cg, and the game's shaders do arithmetic on
large `time` values that half precision cannot hold. So every register and operation
also moves to 32-bit float.

cgcomp's parser also counts the closing bar of |R1.y| as a swizzle letter, which gives
y,x,x,x, so any destination but .x reads the wrong component. This made atan constant in
each quadrant, and split the menu background along the screen's center lines. So swizzles
inside bars get spelled out in full. Vertex programs need this rewrite only.

This takes Cg's assembly, rewrites it, and has cgcomp assemble the result.
"""

import os
import re
import subprocess

# Spelled out so that TEX, whose name ends in X, is not read as "TE" at fixed precision.
OPCODES = "|".join(
    "ABS ADD CMP COS DDX DDY DIV DP2 DP3 DP4 DPH EXP FLR FRC LG2 LRP MAD MAX MIN MOV MUL "
    "NRM POW RCP RFL RSQ SCS SEQ SFL SGE SGT SIN SLE SLT SNE STR SUB".split()
)


def to_full_precision(assembly: str) -> str:
    """Rewrite one fragment program so temporaries are 32-bit and stay off R0."""
    code = "\n".join(line for line in assembly.splitlines() if not line.startswith("#"))
    full_registers = {int(n) for n in re.findall(r"\bR(\d+)\b", code)}
    half_registers = sorted({int(n) for n in re.findall(r"\bH(\d+)\b", code)})
    first_free = max(full_registers, default=-1) + 2
    renamed_half = {number: first_free + position for position, number in enumerate(half_registers)}

    def rename(match: re.Match) -> str:
        number = int(match.group(2))
        return f"R{number + 1}" if match.group(1) == "R" else f"R{renamed_half[number]}"

    lines = []
    for line in assembly.splitlines():
        # cgcomp reads Cg's "#var" and "#const" comments, so they stay.
        if line.startswith("#"):
            lines.append(line)
            continue
        if re.match(r"\s*SHORT TEMP\b", line):
            line = line.replace("SHORT TEMP", "TEMP")
        elif re.match(r"\s*TEMP HC\s*;", line):
            continue
        else:
            line = _instruction_to_full_precision(line)
        lines.append(re.sub(r"\b([RH])(\d+)\b", rename, line))
    return "\n".join(lines) + "\n"


def _instruction_to_full_precision(line: str) -> str:
    line = re.sub(r"\bHC\b", "RC", line)
    line = spell_out_absolute_swizzles(line)
    return re.sub(rf"^(\s*(?:{OPCODES}))[HX](C?(?:_SAT)?\s)", r"\1R\2", line)


def spell_out_absolute_swizzles(assembly: str) -> str:
    """Write |R1.y| as |R1.yyyy|, because cgcomp reads the closing bar as a swizzle letter."""

    def spell_out(match: re.Match) -> str:
        swizzle = match.group(2)
        return f"|{match.group(1)}.{swizzle}{swizzle[-1] * (4 - len(swizzle))}|"

    return re.sub(r"\|([^|,\s]+)\.([xyzw]{1,3})\|", spell_out, assembly)


def _rewrite_vertex_program(assembly: str) -> str:
    lines = [
        line if line.startswith("#") else spell_out_absolute_swizzles(line)
        for line in assembly.splitlines()
    ]
    return "\n".join(lines) + "\n"


def compile_program(stage: str, source: str, output: str, folder: str = ".") -> int:
    """Both paths are relative to folder, because cgcomp cannot open every full Windows path."""
    rewrite = to_full_precision if stage == "-f" else _rewrite_vertex_program

    status = subprocess.call(["cgcomp", stage, "-d", source, output], cwd=folder, stdout=subprocess.DEVNULL)
    if status != 0:
        return status

    with open(os.path.join(folder, output + ".dump"), encoding="utf8") as handle:
        rewritten = rewrite(handle.read())

    assembly_name = output + ".asm"
    with open(os.path.join(folder, assembly_name), "w", encoding="utf8", newline="\n") as handle:
        handle.write(rewritten)

    return subprocess.call(["cgcomp", stage, "-a", assembly_name, output], cwd=folder, stdout=subprocess.DEVNULL)
