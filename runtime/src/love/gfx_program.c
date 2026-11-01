/*
 * Shader programs built by tools/shader_build.py. They are files beside the game's, because
 * they come from the game's own shaders and so cannot ship in the executable. Uniform values
 * live here and go to the vertex program's constants on bind: see tools/shader_translate.py
 * for why they avoid fragment constants.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#include <rsx/rsx.h>
#include <rsx/rsx_program.h>
#include <rsx/gcm_sys.h>

#include "gfx.h"
#include "gfx_internal.h"

#define SHADER_FOLDER "/dev_hdd0/game/BLTR00001/USRDIR/shaders/"
#define NAME_SIZE 32

/* Where each uniform lives inside the float4 slots handed to the vertex program. */
typedef struct {
    char name[NAME_SIZE];
    u8 slot;
    u8 component;
    u8 size;
} ShaderUniform;

typedef struct {
    char name[NAME_SIZE];
    u8 *vertex_program;
    u8 *fragment_program;
    ShaderUniform *uniforms;
    int uniform_count;
    int slot_count;
} ShaderSource;

struct GfxProgram {
    const ShaderSource *source;
    rsxVertexProgram *vertex;
    rsxFragmentProgram *fragment;
    void *vertex_ucode;
    u32 fragment_offset;
    rsxProgramConst *projection;
    rsxProgramConst *screen;
    rsxProgramConst *slots[GFX_MAX_SLOTS];
    f32 values[GFX_MAX_SLOTS][4] __attribute__((aligned(16)));
    bool dirty;
};

static ShaderSource *shader_sources;
static int shader_source_count;
static GfxProgram *programs;
static GfxProgram *current_program;

static u8 *read_program_file(const char *name, const char *extension)
{
    char path[128];
    long size;
    u8 *contents;

    snprintf(path, sizeof(path), SHADER_FOLDER "%s.%s", name, extension);
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        printf("love-ps3: cannot open %s\n", path);
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    contents = memalign(64, size);
    if (contents != NULL && fread(contents, 1, size, file) != (size_t)size)
        contents = NULL;
    fclose(file);
    return contents;
}

static int read_uniforms(FILE *table, ShaderSource *source)
{
    source->uniforms = calloc(source->uniform_count + 1, sizeof(ShaderUniform));
    if (source->uniforms == NULL)
        return -1;

    for (int i = 0; i < source->uniform_count; i++) {
        ShaderUniform *uniform = &source->uniforms[i];
        int slot, component, size;

        if (fscanf(table, "%31s %d %d %d", uniform->name, &slot, &component, &size) != 4)
            return -1;
        uniform->slot = slot;
        uniform->component = component;
        uniform->size = size;
    }
    return 0;
}

static int read_shader(FILE *table, ShaderSource *source)
{
    if (fscanf(table, "%31s %d %d", source->name, &source->slot_count, &source->uniform_count) != 3)
        return -1;
    if (read_uniforms(table, source) != 0)
        return -1;

    source->vertex_program = read_program_file(source->name, "vpo");
    source->fragment_program = read_program_file(source->name, "fpo");
    return source->vertex_program != NULL && source->fragment_program != NULL ? 0 : -1;
}

static int read_shader_table(void)
{
    FILE *table = fopen(SHADER_FOLDER "table.txt", "r");
    int status = 0;

    if (table == NULL) {
        printf("love-ps3: no shaders in " SHADER_FOLDER "\n");
        return -1;
    }

    if (fscanf(table, "%d", &shader_source_count) != 1)
        status = -1;
    shader_sources = status == 0 ? calloc(shader_source_count, sizeof(ShaderSource)) : NULL;
    if (shader_sources == NULL)
        status = -1;

    for (int i = 0; status == 0 && i < shader_source_count; i++)
        status = read_shader(table, &shader_sources[i]);

    fclose(table);
    return status;
}

static int open_program(GfxProgram *program, const ShaderSource *source)
{
    void *fragment_ucode;
    u32 vertex_size;
    u32 fragment_size;
    char slot_name[16];

    program->source = source;
    program->vertex = (rsxVertexProgram *)source->vertex_program;
    program->fragment = (rsxFragmentProgram *)source->fragment_program;

    rsxVertexProgramGetUCode(program->vertex, &program->vertex_ucode, &vertex_size);
    program->projection = rsxVertexProgramGetConst(program->vertex, "projection");
    program->screen = rsxVertexProgramGetConst(program->vertex, "screen");

    for (int slot = 0; slot < source->slot_count && slot < GFX_MAX_SLOTS; slot++) {
        snprintf(slot_name, sizeof(slot_name), "slot%d", slot);
        program->slots[slot] = rsxVertexProgramGetConst(program->vertex, slot_name);
    }

    rsxFragmentProgramGetUCode(program->fragment, &fragment_ucode, &fragment_size);
    void *fragment_memory = rsxMemalign(64, fragment_size);
    if (fragment_memory == NULL)
        return -1;
    memcpy(fragment_memory, fragment_ucode, fragment_size);
    return rsxAddressToOffset(fragment_memory, &program->fragment_offset);
}

int gfx_programs_open(void)
{
    if (read_shader_table() != 0)
        return -1;

    programs = calloc(shader_source_count, sizeof(GfxProgram));
    if (programs == NULL)
        return -1;

    for (int i = 0; i < shader_source_count; i++) {
        if (open_program(&programs[i], &shader_sources[i]) != 0)
            return -1;
    }

    current_program = gfx_program_default();
    return 0;
}

GfxProgram *gfx_program_find(const char *name)
{
    for (int i = 0; i < shader_source_count; i++) {
        if (strcmp(shader_sources[i].name, name) == 0)
            return &programs[i];
    }
    return NULL;
}

GfxProgram *gfx_program_default(void)
{
    return gfx_program_find("default");
}

/* The game sends uniforms that a stage optimised away, and LÖVE would ignore those too. */
void gfx_program_set_uniform(GfxProgram *program, const char *name, const f32 *values, int count)
{
    const ShaderSource *source = program->source;

    for (int i = 0; i < source->uniform_count; i++) {
        const ShaderUniform *uniform = &source->uniforms[i];

        if (strcmp(uniform->name, name) != 0)
            continue;

        for (int c = 0; c < uniform->size && c < count; c++)
            program->values[uniform->slot][uniform->component + c] = values[c];
        program->dirty = true;
        return;
    }
}

void gfx_set_program(GfxProgram *program)
{
    current_program = program != NULL ? program : gfx_program_default();
}

GfxProgram *gfx_get_program(void)
{
    return current_program;
}

bool gfx_program_is_dirty(const GfxProgram *program)
{
    return program->dirty;
}

void gfx_program_bind(GfxProgram *program, u32 target_width, u32 target_height)
{
    f32 projection[4] __attribute__((aligned(16))) = {
        2.0f / target_width, -2.0f / target_height, -1.0f, 1.0f
    };
    f32 screen[4] __attribute__((aligned(16))) = {
        (f32)target_width, (f32)target_height, 0.0f, 0.0f
    };

    rsxLoadVertexProgram(gfx_context, program->vertex, program->vertex_ucode);
    rsxLoadFragmentProgramLocation(gfx_context, program->fragment, program->fragment_offset, GCM_LOCATION_RSX);

    if (program->projection != NULL)
        rsxSetVertexProgramParameter(gfx_context, program->vertex, program->projection, projection);
    if (program->screen != NULL)
        rsxSetVertexProgramParameter(gfx_context, program->vertex, program->screen, screen);

    for (int slot = 0; slot < program->source->slot_count && slot < GFX_MAX_SLOTS; slot++) {
        if (program->slots[slot] != NULL)
            rsxSetVertexProgramParameter(gfx_context, program->vertex, program->slots[slot], program->values[slot]);
    }

    program->dirty = false;
}
