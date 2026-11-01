/*
 * love.graphics state and primitives, on top of gfx_*: the transform stack, colour, and
 * shapes. Vertices leave here already transformed, because the game's shaders expect
 * pixel positions in `vertex_position`, as LÖVE's own batching provides.
 */
#include <math.h>
#include <string.h>

#include <lua.h>
#include <lauxlib.h>

#include "gfx.h"
#include "lua_graphics.h"

#define TRANSFORM_STACK_DEPTH 128
#define CORNER_SEGMENTS       6
#define MAX_SHAPE_POINTS      512
#define MITER_LIMIT           4.0f

static Affine transform_stack[TRANSFORM_STACK_DEPTH];
static int transform_top;
static f32 current_color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
static f32 line_width = 1.0f;

const Affine *graphics_transform(void) { return &transform_stack[transform_top]; }
const f32 *graphics_color(void) { return current_color; }

void affine_multiply(Affine *result, const Affine *left, const Affine *right)
{
    Affine combined;

    combined.a = left->a * right->a + left->c * right->b;
    combined.b = left->b * right->a + left->d * right->b;
    combined.c = left->a * right->c + left->c * right->d;
    combined.d = left->b * right->c + left->d * right->d;
    combined.e = left->a * right->e + left->c * right->f + left->e;
    combined.f = left->b * right->e + left->d * right->f + left->f;
    *result = combined;
}

void write_vertex(GfxVertex *vertex, const Affine *m, f32 x, f32 y, f32 u, f32 v, const f32 *color)
{
    vertex->x = m->a * x + m->c * y + m->e;
    vertex->y = m->b * x + m->d * y + m->f;
    vertex->u = u;
    vertex->v = v;
    vertex->r = color[0];
    vertex->g = color[1];
    vertex->b = color[2];
    vertex->a = color[3];
}

static int g_push(lua_State *L)
{
    if (transform_top + 1 >= TRANSFORM_STACK_DEPTH)
        return luaL_error(L, "love.graphics.push: stack overflow");
    transform_stack[transform_top + 1] = transform_stack[transform_top];
    transform_top++;
    return 0;
}

static int g_pop(lua_State *L)
{
    if (transform_top == 0)
        return luaL_error(L, "love.graphics.pop: stack underflow");
    transform_top--;
    return 0;
}

static int g_origin(lua_State *L)
{
    (void)L;
    Affine identity = { 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f };

    transform_stack[transform_top] = identity;
    return 0;
}

static int g_translate(lua_State *L)
{
    Affine *m = &transform_stack[transform_top];
    f32 x = (f32)luaL_checknumber(L, 1);
    f32 y = (f32)luaL_checknumber(L, 2);

    m->e += m->a * x + m->c * y;
    m->f += m->b * x + m->d * y;
    return 0;
}

static int g_scale(lua_State *L)
{
    Affine *m = &transform_stack[transform_top];
    f32 sx = (f32)luaL_checknumber(L, 1);
    f32 sy = (f32)luaL_optnumber(L, 2, sx);

    m->a *= sx;
    m->b *= sx;
    m->c *= sy;
    m->d *= sy;
    return 0;
}

static int g_rotate(lua_State *L)
{
    Affine *m = &transform_stack[transform_top];
    f32 angle = (f32)luaL_checknumber(L, 1);
    f32 cosine = cosf(angle);
    f32 sine = sinf(angle);
    Affine rotated = *m;

    rotated.a = m->a * cosine + m->c * sine;
    rotated.b = m->b * cosine + m->d * sine;
    rotated.c = m->c * cosine - m->a * sine;
    rotated.d = m->d * cosine - m->b * sine;
    *m = rotated;
    return 0;
}

void read_color_table(lua_State *L, int index, f32 *color)
{
    for (int i = 0; i < 4; i++) {
        lua_rawgeti(L, index, i + 1);
        color[i] = (f32)luaL_optnumber(L, -1, 1.0);
        lua_pop(L, 1);
    }
}

static int g_setColor(lua_State *L)
{
    if (lua_istable(L, 1)) {
        read_color_table(L, 1, current_color);
        return 0;
    }

    current_color[0] = (f32)luaL_checknumber(L, 1);
    current_color[1] = (f32)luaL_checknumber(L, 2);
    current_color[2] = (f32)luaL_checknumber(L, 3);
    current_color[3] = (f32)luaL_optnumber(L, 4, 1.0);
    return 0;
}

static int g_clear(lua_State *L)
{
    f32 color[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

    if (lua_istable(L, 1)) {
        read_color_table(L, 1, color);
    } else if (lua_gettop(L) >= 3) {
        color[0] = (f32)luaL_checknumber(L, 1);
        color[1] = (f32)luaL_checknumber(L, 2);
        color[2] = (f32)luaL_checknumber(L, 3);
        color[3] = (f32)luaL_optnumber(L, 4, 1.0);
    }

    gfx_clear(color[0], color[1], color[2], color[3]);
    return 0;
}

static int g_setLineWidth(lua_State *L)
{
    line_width = (f32)luaL_checknumber(L, 1);
    return 0;
}

static void fill_fan(const f32 *points, int count)
{
    if (count < 3)
        return;

    const Affine *m = graphics_transform();
    GfxVertex *v = gfx_reserve(NULL, (count - 2) * 3);

    for (int i = 1; i < count - 1; i++) {
        write_vertex(v++, m, points[0], points[1], 0.5f, 0.5f, current_color);
        write_vertex(v++, m, points[i * 2], points[i * 2 + 1], 0.5f, 0.5f, current_color);
        write_vertex(v++, m, points[i * 2 + 2], points[i * 2 + 3], 0.5f, 0.5f, current_color);
    }
}

/* Miter offset at a corner: the sum of the neighbouring segment normals, scaled so the
 * stroke keeps its width, and limited so sharp corners do not spike. */
static void miter_offset(const f32 *points, int count, int index, f32 half_width, f32 *out)
{
    int previous = (index + count - 1) % count;
    int next = (index + 1) % count;
    f32 ax = points[index * 2] - points[previous * 2];
    f32 ay = points[index * 2 + 1] - points[previous * 2 + 1];
    f32 bx = points[next * 2] - points[index * 2];
    f32 by = points[next * 2 + 1] - points[index * 2 + 1];
    f32 a_length = sqrtf(ax * ax + ay * ay);
    f32 b_length = sqrtf(bx * bx + by * by);

    if (a_length < 1e-6f) a_length = 1.0f;
    if (b_length < 1e-6f) b_length = 1.0f;

    f32 nx = -ay / a_length - by / b_length;
    f32 ny = ax / a_length + bx / b_length;
    f32 n_length = sqrtf(nx * nx + ny * ny);

    if (n_length < 1e-6f) {
        out[0] = -ay / a_length * half_width;
        out[1] = ax / a_length * half_width;
        return;
    }

    nx /= n_length;
    ny /= n_length;

    f32 cosine_half = nx * (-ay / a_length) + ny * (ax / a_length);
    f32 scale = cosine_half > 1.0f / MITER_LIMIT ? half_width / cosine_half : half_width * MITER_LIMIT;

    out[0] = nx * scale;
    out[1] = ny * scale;
}

static void stroke_closed(const f32 *points, int count)
{
    if (count < 2)
        return;

    const Affine *m = graphics_transform();
    f32 half_width = line_width * 0.5f;
    GfxVertex *v = gfx_reserve(NULL, count * 6);

    for (int i = 0; i < count; i++) {
        int next = (i + 1) % count;
        f32 from[2];
        f32 to[2];

        miter_offset(points, count, i, half_width, from);
        miter_offset(points, count, next, half_width, to);

        f32 x0 = points[i * 2], y0 = points[i * 2 + 1];
        f32 x1 = points[next * 2], y1 = points[next * 2 + 1];

        write_vertex(v++, m, x0 + from[0], y0 + from[1], 0.5f, 0.5f, current_color);
        write_vertex(v++, m, x0 - from[0], y0 - from[1], 0.5f, 0.5f, current_color);
        write_vertex(v++, m, x1 + to[0], y1 + to[1], 0.5f, 0.5f, current_color);
        write_vertex(v++, m, x1 + to[0], y1 + to[1], 0.5f, 0.5f, current_color);
        write_vertex(v++, m, x0 - from[0], y0 - from[1], 0.5f, 0.5f, current_color);
        write_vertex(v++, m, x1 - to[0], y1 - to[1], 0.5f, 0.5f, current_color);
    }
}

static void draw_shape(const char *mode, const f32 *points, int count)
{
    if (strcmp(mode, "line") == 0)
        stroke_closed(points, count);
    else
        fill_fan(points, count);
}

static int add_corner(f32 *points, int count, f32 cx, f32 cy, f32 rx, f32 ry, f32 start_angle)
{
    for (int i = 0; i <= CORNER_SEGMENTS; i++) {
        f32 angle = start_angle + (f32)M_PI_2 * i / CORNER_SEGMENTS;

        points[count * 2] = cx + cosf(angle) * rx;
        points[count * 2 + 1] = cy + sinf(angle) * ry;
        count++;
    }
    return count;
}

static int g_rectangle(lua_State *L)
{
    const char *mode = luaL_checkstring(L, 1);
    f32 x = (f32)luaL_checknumber(L, 2);
    f32 y = (f32)luaL_checknumber(L, 3);
    f32 w = (f32)luaL_checknumber(L, 4);
    f32 h = (f32)luaL_checknumber(L, 5);
    f32 rx = (f32)luaL_optnumber(L, 6, 0.0);
    f32 ry = (f32)luaL_optnumber(L, 7, rx);
    f32 points[(CORNER_SEGMENTS + 1) * 4 * 2];
    int count = 0;

    if (rx > fabsf(w) * 0.5f) rx = fabsf(w) * 0.5f;
    if (ry > fabsf(h) * 0.5f) ry = fabsf(h) * 0.5f;

    if (rx <= 0.0f || ry <= 0.0f) {
        f32 corners[8] = { x, y, x + w, y, x + w, y + h, x, y + h };

        draw_shape(mode, corners, 4);
        return 0;
    }

    count = add_corner(points, count, x + w - rx, y + ry, rx, ry, (f32)-M_PI_2);
    count = add_corner(points, count, x + w - rx, y + h - ry, rx, ry, 0.0f);
    count = add_corner(points, count, x + rx, y + h - ry, rx, ry, (f32)M_PI_2);
    count = add_corner(points, count, x + rx, y + ry, rx, ry, (f32)M_PI);
    draw_shape(mode, points, count);
    return 0;
}

static int g_polygon(lua_State *L)
{
    static f32 points[MAX_SHAPE_POINTS * 2];
    const char *mode = luaL_checkstring(L, 1);
    int values;

    if (lua_istable(L, 2)) {
        values = (int)lua_objlen(L, 2);
        if (values > MAX_SHAPE_POINTS * 2)
            return luaL_error(L, "love.graphics.polygon: too many points");
        for (int i = 0; i < values; i++) {
            lua_rawgeti(L, 2, i + 1);
            points[i] = (f32)lua_tonumber(L, -1);
            lua_pop(L, 1);
        }
    } else {
        values = lua_gettop(L) - 1;
        if (values > MAX_SHAPE_POINTS * 2)
            return luaL_error(L, "love.graphics.polygon: too many points");
        for (int i = 0; i < values; i++)
            points[i] = (f32)luaL_checknumber(L, i + 2);
    }

    draw_shape(mode, points, values / 2);
    return 0;
}

static int g_present(lua_State *L)
{
    (void)L;
    gfx_present();
    return 0;
}

static int g_getWidth(lua_State *L)
{
    lua_pushnumber(L, gfx_screen_width());
    return 1;
}

static int g_getHeight(lua_State *L)
{
    lua_pushnumber(L, gfx_screen_height());
    return 1;
}

static int g_getDimensions(lua_State *L)
{
    lua_pushnumber(L, gfx_screen_width());
    lua_pushnumber(L, gfx_screen_height());
    return 2;
}

static int g_true(lua_State *L)
{
    lua_pushboolean(L, 1);
    return 1;
}

static int g_nothing(lua_State *L)
{
    (void)L;
    return 0;
}

static int g_drawCalls(lua_State *L)
{
    lua_pushnumber(L, gfx_draw_calls_last_frame());
    return 1;
}

static int g_debugDumpScreen(lua_State *L)
{
    lua_pushboolean(L, gfx_debug_dump_screen(luaL_checkstring(L, 1)) == 0);
    return 1;
}

static const luaL_Reg graphics_library[] = {
    { "debugDumpScreen", g_debugDumpScreen },
    { "push",         g_push         },
    { "pop",          g_pop          },
    { "origin",       g_origin       },
    { "translate",    g_translate    },
    { "scale",        g_scale        },
    { "rotate",       g_rotate       },
    { "setColor",     g_setColor     },
    { "clear",        g_clear        },
    { "setLineWidth", g_setLineWidth },
    { "setLineStyle", g_nothing      },
    { "rectangle",    g_rectangle    },
    { "polygon",      g_polygon      },
    { "present",      g_present      },
    { "getWidth",     g_getWidth     },
    { "getHeight",    g_getHeight    },
    { "getDimensions", g_getDimensions },
    { "isActive",     g_true         },
    { "isCreated",    g_true         },
    { "reset",        g_origin       },
    { "discard",      g_nothing      },
    { "drawCalls",    g_drawCalls    },
    { NULL, NULL }
};

int luaopen_love_graphics(lua_State *L)
{
    transform_top = 0;
    g_origin(L);

    if (gfx_open() != 0)
        return luaL_error(L, "love.graphics: RSX setup failed");

    luaL_register(L, "love_graphics_native", graphics_library);
    lua_graphics_register_objects(L);
    return 1;
}
