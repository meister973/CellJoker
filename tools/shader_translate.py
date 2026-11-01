"""Translate the game's LÖVE GLSL shaders to Cg for cgcomp.

Homebrew has no runtime shader compiler, so `love.graphics.newShader` becomes a lookup
of programs built here.

Uniforms do not become fragment constants. RSX patches those into the microcode, and
the game sends about ten values for each sprite (engine/sprite.lua:96-106). They go in
as vertex constants instead, and reach the fragment program through interpolators.
"""

from __future__ import annotations

import os
import re
from dataclasses import dataclass, field

FIRST_SLOT_TEXCOORD = 2
MAX_FRAGMENT_SLOTS = 6
COMPONENTS = "xyzw"

TYPE_SIZES = {"number": 1, "float": 1, "bool": 1, "vec2": 2, "vec3": 3, "vec4": 4}

PRELUDE = """\
float glsl_mod(float a, float b) { return a - b * floor(a / b); }
float2 glsl_mod(float2 a, float b) { return a - b * floor(a / b); }
float3 glsl_mod(float3 a, float b) { return a - b * floor(a / b); }
"""

DEFAULT_EFFECT = """\
float4 effect(float4 colour, sampler2D love_image, float2 texture_coords, float2 screen_coords)
{
    return colour * tex2D(love_image, texture_coords);
}
"""

# GLSL names with a different spelling in Cg. `texture` is reserved in Cg.
RENAMES = {
    "vec2": "float2",
    "vec3": "float3",
    "vec4": "float4",
    "number": "float",
    "Image": "sampler2D",
    "Texel": "tex2D",
    "mix": "lerp",
    "fract": "frac",
    "mod": "glsl_mod",
    "atan": "atan2",
    "texture": "love_image",
}


@dataclass(frozen=True)
class Uniform:
    name: str
    glsl_type: str
    size: int
    slot: int
    component: int


@dataclass
class TranslatedShader:
    name: str
    uniforms: list = field(default_factory=list)
    fragment_slots: int = 0
    slot_count: int = 0
    fragment: str = ""
    vertex: str = ""


class ShaderTranslateError(Exception):
    """Raised when a shader cannot fit the RSX interpolator budget."""


def translate(name: str, source: str) -> TranslatedShader:
    body = re.sub(r"#if defined\(VERTEX\).*?#endif", "", source, count=1, flags=re.S)
    vertex_match = re.search(r"#ifdef VERTEX(.*?)#endif", body, flags=re.S)
    vertex_body = vertex_match.group(1) if vertex_match else ""
    fragment_body = body.replace(vertex_match.group(0), "") if vertex_match else body

    declared = _find_uniforms(fragment_body) + _find_uniforms(vertex_body)
    fragment_code = _to_cg(_strip_uniforms(fragment_body))
    vertex_code = _to_cg(_strip_uniforms(vertex_body))
    defines = "".join(re.findall(r"^[ \t]*#define.*\n", _to_cg(fragment_body), flags=re.M))

    if "effect" not in fragment_code:
        fragment_code += DEFAULT_EFFECT

    uniforms, fragment_slots, slot_count = _pack(declared, fragment_code)
    return TranslatedShader(
        name=name,
        uniforms=uniforms,
        fragment_slots=fragment_slots,
        slot_count=slot_count,
        fragment=_fragment_program(uniforms, fragment_slots, fragment_code),
        vertex=_vertex_program(uniforms, fragment_slots, slot_count, defines + vertex_code),
    )


def default_shader() -> TranslatedShader:
    return TranslatedShader(
        name="default",
        fragment=_fragment_program([], 0, DEFAULT_EFFECT),
        vertex=_vertex_program([], 0, 0, ""),
    )


def shader_table(shaders: list) -> str:
    """The text gfx_program.c reads: how many shaders, then each one and its uniforms."""
    lines = [str(len(shaders))]
    for shader in shaders:
        lines.append(f"{shader.name} {shader.slot_count} {len(shader.uniforms)}")
        for uniform in shader.uniforms:
            lines.append(f"{uniform.name} {uniform.slot} {uniform.component} {uniform.size}")
    return "\n".join(lines) + "\n"


def _find_uniforms(code: str) -> list:
    pattern = r"extern\s+(?:MY_HIGHP_OR_MEDIUMP\s+|highp\s+|mediump\s+)?(\w+)\s+(\w+)\s*;"
    return re.findall(pattern, code)


def _strip_uniforms(code: str) -> str:
    return re.sub(r"extern\s+[^;]*;", "", code)


def _to_cg(code: str) -> str:
    code = re.sub(r"\b(MY_HIGHP_OR_MEDIUMP|highp|mediump)\b", "", code)
    code = re.sub(r"\b(\w+)\b", lambda match: RENAMES.get(match.group(1), match.group(1)), code)
    code = re.sub(
        r"float4\s+position\(\s*mat4\s+transform_projection\s*,\s*float4\s+vertex_position\s*\)",
        "float4 position(float4 vertex_position)",
        code,
    )
    code = code.replace("transform_projection * vertex_position", "project(vertex_position)")
    return _expand_single_argument_vectors(code)


def _expand_single_argument_vectors(code: str) -> str:
    """Cg has no float3(x) splat, so it becomes float3(x, x, x)."""
    result = []
    cursor = 0

    for match in re.finditer(r"\bfloat([234])\(", code):
        if match.start() < cursor:
            continue
        argument_end = _closing_parenthesis(code, match.end())
        argument = code[match.end():argument_end]
        if _has_top_level_comma(argument):
            continue
        repeated = ", ".join([argument.strip()] * int(match.group(1)))
        result.append(code[cursor:match.end()])
        result.append(repeated)
        cursor = argument_end

    result.append(code[cursor:])
    return "".join(result)


def _closing_parenthesis(code: str, start: int) -> int:
    depth = 1
    for index in range(start, len(code)):
        if code[index] == "(":
            depth += 1
        elif code[index] == ")":
            depth -= 1
            if depth == 0:
                return index
    raise ShaderTranslateError("unbalanced parenthesis")


def _has_top_level_comma(argument: str) -> bool:
    depth = 0
    for character in argument:
        depth += character == "("
        depth -= character == ")"
        if character == "," and depth == 0:
            return True
    return False


def _is_used(name: str, code: str) -> bool:
    return re.search(rf"\b{name}\b", code) is not None


def _pack(declared: list, fragment_code: str) -> tuple:
    unique = list(dict(((name, glsl_type) for glsl_type, name in declared)).items())
    in_fragment = [item for item in unique if _is_used(item[0], fragment_code)]
    vertex_only = [item for item in unique if not _is_used(item[0], fragment_code)]

    uniforms, fragment_slots = _pack_group(in_fragment, 0)
    vertex_uniforms, slot_count = _pack_group(vertex_only, fragment_slots)

    if fragment_slots > MAX_FRAGMENT_SLOTS:
        raise ShaderTranslateError(f"{fragment_slots} interpolator slots needed")
    return uniforms + vertex_uniforms, fragment_slots, slot_count


def _pack_group(items: list, first_slot: int) -> tuple:
    free = []
    uniforms = []

    for name, glsl_type in sorted(items, key=lambda item: -TYPE_SIZES[item[1]]):
        size = TYPE_SIZES[glsl_type]
        for index, available in enumerate(free):
            if available >= size:
                break
        else:
            free.append(4)
            index = len(free) - 1
        uniforms.append(Uniform(name, glsl_type, size, first_slot + index, 4 - free[index]))
        free[index] -= size

    return uniforms, first_slot + len(free)


def _cg_type(uniform: Uniform) -> str:
    if uniform.glsl_type == "bool":
        return "bool"
    return "float" if uniform.size == 1 else f"float{uniform.size}"


def _read_slot(uniform: Uniform, slot_name: str) -> str:
    swizzle = COMPONENTS[uniform.component:uniform.component + uniform.size]
    value = f"{slot_name}.{swizzle}"
    return f"{value} > 0.5" if uniform.glsl_type == "bool" else value


def _fragment_program(uniforms: list, fragment_slots: int, code: str) -> str:
    used = [uniform for uniform in uniforms if uniform.slot < fragment_slots]
    statics = "".join(f"static {_cg_type(u)} {u.name};\n" for u in used)
    inputs = "".join(
        f"    float4 v_slot{slot} : TEXCOORD{slot + FIRST_SLOT_TEXCOORD},\n"
        for slot in range(fragment_slots)
    )
    assignments = "".join(f"    {u.name} = {_read_slot(u, f'v_slot{u.slot}')};\n" for u in used)

    return (
        f"{PRELUDE}static float4 love_ScreenSize;\n{statics}\n{code}\n"
        "void main(\n"
        "    float4 v_colour : COLOR0,\n"
        "    float2 v_uv : TEXCOORD0,\n"
        "    float4 v_screen : TEXCOORD1,\n"
        f"{inputs}"
        "    uniform sampler2D image,\n"
        "    out float4 o_colour : COLOR)\n"
        "{\n"
        "    love_ScreenSize = float4(v_screen.z, v_screen.w, 0.0, 0.0);\n"
        f"{assignments}"
        "    o_colour = effect(v_colour, image, v_uv, v_screen.xy);\n"
        "}\n"
    )


def _vertex_program(uniforms: list, fragment_slots: int, slot_count: int, code: str) -> str:
    used = [uniform for uniform in uniforms if _is_used(uniform.name, code)]
    statics = "".join(f"static {_cg_type(u)} {u.name};\n" for u in used)
    assignments = "".join(f"    {u.name} = {_read_slot(u, f'slot{u.slot}')};\n" for u in used)
    constants = "".join(f"    uniform float4 slot{slot},\n" for slot in range(slot_count))
    outputs = "".join(
        f"    out float4 o_slot{slot} : TEXCOORD{slot + FIRST_SLOT_TEXCOORD},\n"
        for slot in range(fragment_slots)
    )
    forwards = "".join(f"    o_slot{slot} = slot{slot};\n" for slot in range(fragment_slots))
    place = "position(a_position)" if "position(" in code else "project(a_position)"

    return (
        "static float4 love_projection;\nstatic float4 love_ScreenSize;\n"
        f"{statics}\n"
        "float4 project(float4 v)\n"
        "{\n"
        "    return float4(v.x * love_projection.x + love_projection.z,\n"
        "                  v.y * love_projection.y + love_projection.w, 0.0, 1.0);\n"
        "}\n\n"
        f"{code}\n"
        "void main(\n"
        "    float4 a_position : POSITION,\n"
        "    float2 a_uv : TEXCOORD0,\n"
        "    float4 a_colour : COLOR0,\n"
        "    uniform float4 projection,\n"
        "    uniform float4 screen,\n"
        f"{constants}"
        f"{outputs}"
        "    out float4 o_position : POSITION,\n"
        "    out float4 o_colour : COLOR0,\n"
        "    out float2 o_uv : TEXCOORD0,\n"
        "    out float4 o_screen : TEXCOORD1)\n"
        "{\n"
        "    love_projection = projection;\n"
        "    love_ScreenSize = screen;\n"
        f"{assignments}"
        f"    o_position = {place};\n"
        "    o_colour = a_colour;\n"
        "    o_uv = a_uv;\n"
        "    o_screen = float4(a_position.x, a_position.y, screen.x, screen.y);\n"
        f"{forwards}"
        "}\n"
    )


def translate_folders(source_folders: list) -> list:
    shaders = [default_shader()]
    for source_folder in source_folders:
        for filename in sorted(os.listdir(source_folder)):
            if filename.endswith(".fs"):
                with open(os.path.join(source_folder, filename), encoding="utf8") as handle:
                    shaders.append(translate(filename[:-3], handle.read()))
    return shaders
