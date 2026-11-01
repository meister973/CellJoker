/* love.graphics objects: images, canvases, quads, fonts, text, shaders, and draw. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lua.h>
#include <lauxlib.h>

#include "gfx.h"
#include "lua_graphics.h"

#define TEXTURE_TYPE "love.Texture"
#define QUAD_TYPE    "love.Quad"
#define FONT_TYPE    "love.Font"
#define TEXT_TYPE    "love.Text"
#define SHADER_TYPE  "love.Shader"
#define IMAGE_CACHE  "love.ImageCache"

#define MAX_FONTS 8
#define MAX_UNIFORM_VALUES 16

typedef struct {
    f32 x, y, width, height;
    f32 source_width, source_height;
} Quad;

typedef struct {
    GfxFont *font;
    f32 tint[4];
    char *text;
} Text;

typedef struct {
    char path[128];
    u32 size;
    GfxFont *font;
} FontCacheEntry;

static FontCacheEntry font_cache[MAX_FONTS];
static int font_cache_count;
static GfxFont *current_font;
static bool default_nearest;

static void *test_type(lua_State *L, int index, const char *type_name)
{
    void *value = lua_touserdata(L, index);

    if (value == NULL || !lua_getmetatable(L, index))
        return NULL;

    lua_getfield(L, LUA_REGISTRYINDEX, type_name);
    int matches = lua_rawequal(L, -1, -2);
    lua_pop(L, 2);
    return matches ? value : NULL;
}

static void push_pointer(lua_State *L, void *pointer, const char *type_name)
{
    void **slot = lua_newuserdata(L, sizeof(void *));

    *slot = pointer;
    luaL_getmetatable(L, type_name);
    lua_setmetatable(L, -2);
}

static void install_path(lua_State *L, char *buffer, size_t size, const char *relative)
{
    lua_getglobal(L, "INSTALL_ROOT");
    snprintf(buffer, size, "%s%s", lua_tostring(L, -1), relative);
    lua_pop(L, 1);
}

/* ------------------------------------------------------------------ textures */

static GfxTexture *check_texture(lua_State *L, int index)
{
    return *(GfxTexture **)luaL_checkudata(L, index, TEXTURE_TYPE);
}

static f32 option_number(lua_State *L, int table, const char *key, f32 fallback)
{
    if (!lua_istable(L, table))
        return fallback;

    lua_getfield(L, table, key);
    f32 value = (f32)luaL_optnumber(L, -1, fallback);
    lua_pop(L, 1);
    return value;
}

/* Images are cooked at build time, so "x.png" is read from "cooked/x.rtex". */
static int g_newImage(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    f32 dpi_scale = option_number(L, 2, "dpiscale", 1.0f);
    const char *extension = strrchr(path, '.');
    int stem_length = extension != NULL ? (int)(extension - path) : (int)strlen(path);
    char relative[256];
    char full_path[320];

    snprintf(relative, sizeof(relative), "cooked/%.*s.rtex", stem_length, path);
    install_path(L, full_path, sizeof(full_path), relative);

    /* The game reloads every atlas when Pixel Smoothing changes, and 2x is about half of RSX
     * memory, so a second copy would not fit. */
    lua_getfield(L, LUA_REGISTRYINDEX, IMAGE_CACHE);
    lua_pushfstring(L, "%s@%f", full_path, (lua_Number)dpi_scale);
    lua_pushvalue(L, -1);
    lua_rawget(L, -3);

    if (!lua_isnil(L, -1)) {
        check_texture(L, -1)->nearest = default_nearest;
        gfx_texture_changed();
        return 1;
    }
    lua_pop(L, 1);

    GfxTexture *texture = gfx_texture_load_cooked(full_path, dpi_scale);
    if (texture == NULL)
        return luaL_error(L, "love.graphics.newImage: cannot load %s", full_path);

    texture->nearest = default_nearest;
    push_pointer(L, texture, TEXTURE_TYPE);
    lua_pushvalue(L, -1);
    lua_insert(L, -3);
    lua_rawset(L, -4);
    return 1;
}

static int g_newCanvas(lua_State *L)
{
    u32 width = (u32)luaL_optnumber(L, 1, gfx_screen_width());
    u32 height = (u32)luaL_optnumber(L, 2, gfx_screen_height());
    GfxTexture *canvas = gfx_texture_create(width, height, true);

    if (canvas == NULL)
        return luaL_error(L, "love.graphics.newCanvas: out of RSX memory (%dx%d)", width, height);

    push_pointer(L, canvas, TEXTURE_TYPE);
    return 1;
}

static int texture_getWidth(lua_State *L)
{
    lua_pushnumber(L, check_texture(L, 1)->width);
    return 1;
}

static int texture_getHeight(lua_State *L)
{
    lua_pushnumber(L, check_texture(L, 1)->height);
    return 1;
}

static int texture_getDimensions(lua_State *L)
{
    GfxTexture *texture = check_texture(L, 1);

    lua_pushnumber(L, texture->width);
    lua_pushnumber(L, texture->height);
    return 2;
}

static int texture_getPixelWidth(lua_State *L)
{
    lua_pushnumber(L, check_texture(L, 1)->pixel_width);
    return 1;
}

static int texture_getPixelHeight(lua_State *L)
{
    lua_pushnumber(L, check_texture(L, 1)->pixel_height);
    return 1;
}

static int texture_setFilter(lua_State *L)
{
    check_texture(L, 1)->nearest = strcmp(luaL_checkstring(L, 2), "nearest") == 0;
    gfx_texture_changed();
    return 0;
}

static int nothing(lua_State *L)
{
    (void)L;
    return 0;
}

static const luaL_Reg texture_methods[] = {
    { "getWidth",       texture_getWidth       },
    { "getHeight",      texture_getHeight      },
    { "getDimensions",  texture_getDimensions  },
    { "getPixelWidth",  texture_getPixelWidth  },
    { "getPixelHeight", texture_getPixelHeight },
    { "setFilter",      texture_setFilter      },
    { "setWrap",        nothing                },
    { NULL, NULL }
};

static int g_setDefaultFilter(lua_State *L)
{
    default_nearest = strcmp(luaL_checkstring(L, 1), "nearest") == 0;
    return 0;
}

static int g_setCanvas(lua_State *L)
{
    if (lua_isnoneornil(L, 1)) {
        gfx_set_canvas(NULL);
        return 0;
    }

    if (lua_istable(L, 1)) {
        lua_rawgeti(L, 1, 1);
        gfx_set_canvas(check_texture(L, -1));
        lua_pop(L, 1);
        return 0;
    }

    gfx_set_canvas(check_texture(L, 1));
    return 0;
}

/* ------------------------------------------------------------------ quads */

static void read_quad(lua_State *L, Quad *quad, int first)
{
    quad->x = (f32)luaL_checknumber(L, first);
    quad->y = (f32)luaL_checknumber(L, first + 1);
    quad->width = (f32)luaL_checknumber(L, first + 2);
    quad->height = (f32)luaL_checknumber(L, first + 3);
    quad->source_width = (f32)luaL_checknumber(L, first + 4);
    quad->source_height = (f32)luaL_checknumber(L, first + 5);
}

static int g_newQuad(lua_State *L)
{
    Quad *quad = lua_newuserdata(L, sizeof(Quad));

    read_quad(L, quad, 1);
    luaL_getmetatable(L, QUAD_TYPE);
    lua_setmetatable(L, -2);
    return 1;
}

static int quad_setViewport(lua_State *L)
{
    Quad *quad = luaL_checkudata(L, 1, QUAD_TYPE);

    quad->x = (f32)luaL_checknumber(L, 2);
    quad->y = (f32)luaL_checknumber(L, 3);
    quad->width = (f32)luaL_checknumber(L, 4);
    quad->height = (f32)luaL_checknumber(L, 5);
    quad->source_width = (f32)luaL_optnumber(L, 6, quad->source_width);
    quad->source_height = (f32)luaL_optnumber(L, 7, quad->source_height);
    return 0;
}

static const luaL_Reg quad_methods[] = {
    { "setViewport", quad_setViewport },
    { NULL, NULL }
};

/* ------------------------------------------------------------------ fonts and text */

static GfxFont *check_font(lua_State *L, int index)
{
    return *(GfxFont **)luaL_checkudata(L, index, FONT_TYPE);
}

static GfxFont *open_cached_font(lua_State *L, const char *path, u32 size)
{
    for (int i = 0; i < font_cache_count; i++) {
        if (font_cache[i].size == size && strcmp(font_cache[i].path, path) == 0)
            return font_cache[i].font;
    }

    char full_path[320];
    install_path(L, full_path, sizeof(full_path), path);

    GfxFont *font = gfx_font_open(full_path, size);
    if (font == NULL)
        luaL_error(L, "love.graphics.newFont: cannot load %s", full_path);
    if (font_cache_count == MAX_FONTS)
        luaL_error(L, "love.graphics.newFont: too many fonts");

    FontCacheEntry *entry = &font_cache[font_cache_count++];
    snprintf(entry->path, sizeof(entry->path), "%s", path);
    entry->size = size;
    entry->font = font;
    return font;
}

static int g_newFont(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    u32 size = (u32)luaL_optnumber(L, 2, 12);

    push_pointer(L, open_cached_font(L, path, size), FONT_TYPE);
    return 1;
}

static int g_setNewFont(lua_State *L)
{
    g_newFont(L);
    current_font = check_font(L, -1);
    return 1;
}

static int g_setFont(lua_State *L)
{
    current_font = check_font(L, 1);
    return 0;
}

static int font_getWidth(lua_State *L)
{
    lua_pushnumber(L, gfx_font_width(check_font(L, 1), luaL_checkstring(L, 2)));
    return 1;
}

static int font_getHeight(lua_State *L)
{
    lua_pushnumber(L, gfx_font_height(check_font(L, 1)));
    return 1;
}

static const luaL_Reg font_methods[] = {
    { "getWidth",      font_getWidth  },
    { "getHeight",     font_getHeight },
    { "setFilter",     nothing        },
    { "setLineHeight", nothing        },
    { NULL, NULL }
};

/* Accepts a string, or LÖVE's coloured form: {colour, "text", colour, "text", ...}. Only
 * the first colour is kept, because the game passes one colour and one string. */
static const char *read_text_argument(lua_State *L, int index, f32 *tint)
{
    tint[0] = tint[1] = tint[2] = tint[3] = 1.0f;

    if (!lua_istable(L, index))
        return luaL_checkstring(L, index);

    int absolute = index > 0 ? index : lua_gettop(L) + index + 1;
    int length = (int)lua_objlen(L, absolute);
    bool have_color = false;
    luaL_Buffer buffer;

    luaL_buffinit(L, &buffer);
    for (int i = 1; i <= length; i++) {
        lua_rawgeti(L, absolute, i);
        if (lua_istable(L, -1)) {
            if (!have_color) {
                read_color_table(L, lua_gettop(L), tint);
                have_color = true;
            }
            lua_pop(L, 1);
        } else {
            luaL_addvalue(&buffer);
        }
    }
    luaL_pushresult(&buffer);
    lua_replace(L, absolute);
    return lua_tostring(L, absolute);
}

static int store_text(lua_State *L, Text *text, int index)
{
    const char *string = lua_isnoneornil(L, index) ? "" : read_text_argument(L, index, text->tint);
    size_t length = strlen(string);
    char *copy = malloc(length + 1);

    if (copy == NULL)
        return luaL_error(L, "love.graphics: out of memory for text");

    memcpy(copy, string, length + 1);
    free(text->text);
    text->text = copy;
    return 0;
}

static int g_newText(lua_State *L)
{
    GfxFont *font = check_font(L, 1);
    Text *text = lua_newuserdata(L, sizeof(Text));

    text->font = font;
    text->text = NULL;
    text->tint[0] = text->tint[1] = text->tint[2] = text->tint[3] = 1.0f;
    luaL_getmetatable(L, TEXT_TYPE);
    lua_setmetatable(L, -2);
    store_text(L, text, 2);
    return 1;
}

static int text_set(lua_State *L)
{
    return store_text(L, luaL_checkudata(L, 1, TEXT_TYPE), 2);
}

static int text_gc(lua_State *L)
{
    Text *text = luaL_checkudata(L, 1, TEXT_TYPE);

    free(text->text);
    text->text = NULL;
    return 0;
}

static const luaL_Reg text_methods[] = {
    { "set",  text_set },
    { "__gc", text_gc  },
    { NULL, NULL }
};

/* ------------------------------------------------------------------ shaders */

static GfxProgram *check_shader(lua_State *L, int index)
{
    return *(GfxProgram **)luaL_checkudata(L, index, SHADER_TYPE);
}

/* Runtime compilation does not exist here, so "resources/shaders/foil.fs" names a
 * program built by tools/shader_translate.py. */
static int g_newShader(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    const char *slash = strrchr(path, '/');
    const char *name = slash != NULL ? slash + 1 : path;
    const char *extension = strrchr(name, '.');
    char program_name[64];

    snprintf(program_name, sizeof(program_name), "%.*s",
             extension != NULL ? (int)(extension - name) : (int)strlen(name), name);

    GfxProgram *program = gfx_program_find(program_name);
    if (program == NULL)
        return luaL_error(L, "love.graphics.newShader: no program named %s", program_name);

    push_pointer(L, program, SHADER_TYPE);
    return 1;
}

static int shader_send(lua_State *L)
{
    GfxProgram *program = check_shader(L, 1);
    const char *name = luaL_checkstring(L, 2);
    f32 values[MAX_UNIFORM_VALUES];
    int count = 0;

    if (lua_istable(L, 3)) {
        count = (int)lua_objlen(L, 3);
        if (count > MAX_UNIFORM_VALUES)
            count = MAX_UNIFORM_VALUES;
        for (int i = 0; i < count; i++) {
            lua_rawgeti(L, 3, i + 1);
            values[i] = (f32)lua_tonumber(L, -1);
            lua_pop(L, 1);
        }
    } else if (lua_isboolean(L, 3)) {
        values[0] = lua_toboolean(L, 3) ? 1.0f : 0.0f;
        count = 1;
    } else {
        values[0] = (f32)luaL_checknumber(L, 3);
        count = 1;
    }

    gfx_program_set_uniform(program, name, values, count);
    return 0;
}

static const luaL_Reg shader_methods[] = {
    { "send", shader_send },
    { NULL, NULL }
};

static int g_setShader(lua_State *L)
{
    gfx_set_program(lua_isnoneornil(L, 1) ? NULL : check_shader(L, 1));
    return 0;
}

/* ------------------------------------------------------------------ draw */

/* Reads x, y, r, sx, sy, ox, oy, kx, ky, using the same matrix as LÖVE. */
static void read_placement(lua_State *L, int first, Affine *result)
{
    f32 x = (f32)luaL_optnumber(L, first, 0.0);
    f32 y = (f32)luaL_optnumber(L, first + 1, 0.0);
    f32 angle = (f32)luaL_optnumber(L, first + 2, 0.0);
    f32 sx = (f32)luaL_optnumber(L, first + 3, 1.0);
    f32 sy = (f32)luaL_optnumber(L, first + 4, sx);
    f32 ox = (f32)luaL_optnumber(L, first + 5, 0.0);
    f32 oy = (f32)luaL_optnumber(L, first + 6, 0.0);
    f32 kx = (f32)luaL_optnumber(L, first + 7, 0.0);
    f32 ky = (f32)luaL_optnumber(L, first + 8, 0.0);
    f32 cosine = cosf(angle);
    f32 sine = sinf(angle);
    Affine local;

    local.a = cosine * sx - ky * sine * sy;
    local.b = sine * sx + ky * cosine * sy;
    local.c = kx * cosine * sx - sine * sy;
    local.d = kx * sine * sx + cosine * sy;
    local.e = x - ox * local.a - oy * local.c;
    local.f = y - ox * local.b - oy * local.d;

    affine_multiply(result, graphics_transform(), &local);
}

static void write_quad(GfxTexture *texture, const Affine *m, f32 x, f32 y, f32 width, f32 height,
                       f32 u0, f32 v0, f32 u1, f32 v1, const f32 *color)
{
    GfxVertex *v = gfx_reserve(texture, 6);

    write_vertex(v++, m, x, y, u0, v0, color);
    write_vertex(v++, m, x + width, y, u1, v0, color);
    write_vertex(v++, m, x, y + height, u0, v1, color);
    write_vertex(v++, m, x + width, y, u1, v0, color);
    write_vertex(v++, m, x + width, y + height, u1, v1, color);
    write_vertex(v++, m, x, y + height, u0, v1, color);
}

static void draw_string(GfxFont *font, const char *string, const Affine *m, const f32 *color)
{
    f32 pen_x = 0.0f;
    f32 pen_y = 0.0f;

    gfx_hold_batch(true);
    while (*string != '\0') {
        u32 codepoint = gfx_utf8_next(&string);

        if (codepoint == '\n') {
            pen_x = 0.0f;
            pen_y += gfx_font_height(font);
            continue;
        }

        const GfxGlyph *glyph = gfx_font_glyph(font, codepoint);

        if (glyph->width > 0.0f) {
            write_quad(gfx_font_texture(font), m, pen_x + glyph->left, pen_y + glyph->top,
                       glyph->width, glyph->height, glyph->u0, glyph->v0, glyph->u1, glyph->v1, color);
        }
        pen_x += glyph->advance;
    }
    gfx_hold_batch(false);
}

static int g_draw(lua_State *L)
{
    Affine m;
    Text *text = test_type(L, 1, TEXT_TYPE);

    if (text != NULL) {
        const f32 *current = graphics_color();
        f32 color[4];

        for (int i = 0; i < 4; i++)
            color[i] = current[i] * text->tint[i];
        read_placement(L, 2, &m);
        draw_string(text->font, text->text, &m, color);
        return 0;
    }

    GfxTexture *texture = check_texture(L, 1);
    Quad *quad = test_type(L, 2, QUAD_TYPE);

    read_placement(L, quad != NULL ? 3 : 2, &m);

    if (quad == NULL) {
        write_quad(texture, &m, 0.0f, 0.0f, texture->width, texture->height,
                   0.0f, 0.0f, 1.0f, 1.0f, graphics_color());
        return 0;
    }

    write_quad(texture, &m, 0.0f, 0.0f, quad->width, quad->height,
               quad->x / quad->source_width, quad->y / quad->source_height,
               (quad->x + quad->width) / quad->source_width,
               (quad->y + quad->height) / quad->source_height, graphics_color());
    return 0;
}

static int g_print(lua_State *L)
{
    Affine m;
    f32 tint[4];

    if (current_font == NULL)
        return 0;

    const char *string = read_text_argument(L, 1, tint);
    read_placement(L, 2, &m);
    draw_string(current_font, string, &m, graphics_color());
    return 0;
}

/* ------------------------------------------------------------------ registration */

static void register_type(lua_State *L, const char *type_name, const luaL_Reg *methods)
{
    luaL_newmetatable(L, type_name);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    luaL_register(L, NULL, methods);
    lua_pop(L, 1);
}

static const luaL_Reg object_functions[] = {
    { "newImage",         g_newImage         },
    { "newCanvas",        g_newCanvas        },
    { "newQuad",          g_newQuad          },
    { "newFont",          g_newFont          },
    { "setNewFont",       g_setNewFont       },
    { "setFont",          g_setFont          },
    { "newText",          g_newText          },
    { "newShader",        g_newShader        },
    { "setShader",        g_setShader        },
    { "setCanvas",        g_setCanvas        },
    { "setDefaultFilter", g_setDefaultFilter },
    { "draw",             g_draw             },
    { "print",            g_print            },
    { NULL, NULL }
};

void lua_graphics_register_objects(lua_State *L)
{
    register_type(L, TEXTURE_TYPE, texture_methods);
    register_type(L, QUAD_TYPE, quad_methods);
    register_type(L, FONT_TYPE, font_methods);
    register_type(L, TEXT_TYPE, text_methods);
    register_type(L, SHADER_TYPE, shader_methods);
    lua_newtable(L);
    lua_setfield(L, LUA_REGISTRYINDEX, IMAGE_CACHE);
    luaL_register(L, NULL, object_functions);
}
