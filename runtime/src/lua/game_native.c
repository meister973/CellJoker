/*
 * Hot engine functions of the game in C, with the same reads, arithmetic and results as the Lua
 * ones they replace. runtime/lua/native_engine.lua puts them in place. On the host runner,
 * HARNESS_COMPARE_GAME=1 runs both versions on every call and stops at the first difference.
 */
#include <math.h>

#include <lua.h>
#include <lauxlib.h>

#include "lua_runtime.h"

/* Lua 5.1's math.pi, so `angle + math.pi/2` rounds as it does in Lua. */
static const lua_Number PI = 3.14159265358979323846;

/* The field names, made once and kept as upvalues: lua_getfield would hash and intern its key
 * string on every call. */
enum Key { KEY_CONTAINER, KEY_CT, KEY_T, KEY_STATES, KEY_HOVER, KEY_IS, KEY_G, KEY_COLLISION_BUFFER,
    KEY_X, KEY_Y, KEY_W, KEY_H, KEY_R, KEY_COUNT };

static const char *const KEY_NAMES[KEY_COUNT] = { "container", "CT", "T", "states", "hover", "is",
    "G", "COLLISION_BUFFER", "x", "y", "w", "h", "r" };

typedef struct {
    lua_Number x, y, w, h, r;
} Rect;

/* Pushes table[key], with metamethods, as lua_getfield would. `table` is an absolute index. */
static void get_field(lua_State *L, int table, enum Key key)
{
    lua_pushvalue(L, lua_upvalueindex(key + 1));
    lua_gettable(L, table);
}

/* Pops the number on top of the stack, with an error where Lua arithmetic would raise one. */
static lua_Number pop_number(lua_State *L, enum Key key)
{
    if (!lua_isnumber(L, -1))
        luaL_error(L, "collides_with_point: %s is %s, not a number", KEY_NAMES[key], luaL_typename(L, -1));
    lua_Number value = lua_tonumber(L, -1);
    lua_pop(L, 1);
    return value;
}

static lua_Number number_field(lua_State *L, int table, enum Key key)
{
    get_field(L, table, key);
    return pop_number(L, key);
}

static Rect read_rect(lua_State *L, int table)
{
    Rect rect;

    rect.x = number_field(L, table, KEY_X);
    rect.y = number_field(L, table, KEY_Y);
    rect.w = number_field(L, table, KEY_W);
    rect.h = number_field(L, table, KEY_H);
    rect.r = number_field(L, table, KEY_R);
    return rect;
}

/* `self.states.hover.is and G.COLLISION_BUFFER or 0` */
static lua_Number hover_buffer(lua_State *L, int self)
{
    get_field(L, self, KEY_STATES);
    get_field(L, lua_gettop(L), KEY_HOVER);
    get_field(L, lua_gettop(L), KEY_IS);
    int hovered = lua_toboolean(L, -1);
    lua_pop(L, 3);
    if (!hovered)
        return 0;

    get_field(L, LUA_GLOBALSINDEX, KEY_G);
    get_field(L, lua_gettop(L), KEY_COLLISION_BUFFER);
    if (!lua_toboolean(L, -1)) {
        lua_pop(L, 2);
        return 0;
    }
    lua_Number buffer = pop_number(L, KEY_COLLISION_BUFFER);
    lua_pop(L, 1);
    return buffer;
}

/* point_rotate (functions/misc_functions.lua) */
static void rotate_point(lua_Number *x, lua_Number *y, lua_Number angle)
{
    lua_Number cosine = cos(angle + PI / 2);
    lua_Number sine = sin(angle + PI / 2);
    lua_Number old_x = *x;
    lua_Number old_y = *y;

    *x = -old_y * cosine + old_x * sine;
    *y = old_y * sine + old_x * cosine;
}

/* The cursor in the container's frame, moved as Node:collides_with_point (engine/node.lua)
 * moves it, including its rotation for a container that is almost not rotated. */
static void into_container(const Rect *container, lua_Number *x, lua_Number *y)
{
    if (fabs(container->r) < 0.1) {
        *x = *x + -container->w / 2;
        *y = *y + -container->h / 2;
        rotate_point(x, y, container->r);
        *x = *x + (container->w / 2 - container->x);
        *y = *y + (container->h / 2 - container->y);
        return;
    }
    *x = *x + -container->x;
    *y = *y + -container->y;
}

/* The cursor turned about the centre of a rotated node, as the Lua version turns it. */
static void unrotate(const Rect *rect, lua_Number *x, lua_Number *y)
{
    lua_Number cosine = cos(rect->r + PI / 2);
    lua_Number sine = sin(rect->r + PI / 2);
    lua_Number dx = *x - (rect->x + 0.5 * rect->w);
    lua_Number dy = *y - (rect->y + 0.5 * rect->h);
    lua_Number turned_x = dy * cosine - dx * sine;
    lua_Number turned_y = dy * sine + dx * cosine;

    *x = turned_x + (rect->x + 0.5 * rect->w);
    *y = turned_y + (rect->y + 0.5 * rect->h);
}

static int inside(const Rect *rect, lua_Number buffer, lua_Number x, lua_Number y)
{
    return x >= rect->x - buffer && y >= rect->y - buffer
        && x <= rect->x + rect->w + buffer && y <= rect->y + rect->h + buffer;
}

/* Node:collides_with_point(point): true, or nothing. */
static int collides_with_point(lua_State *L)
{
    enum { SELF = 1, POINT, CONTAINER, T };

    lua_settop(L, POINT);
    get_field(L, SELF, KEY_CONTAINER);
    if (!lua_toboolean(L, CONTAINER))
        return 0;

    get_field(L, SELF, KEY_CT);
    if (!lua_toboolean(L, T)) {
        lua_pop(L, 1);
        get_field(L, SELF, KEY_T);
    }
    Rect rect = read_rect(L, T);
    lua_Number buffer = hover_buffer(L, SELF);
    lua_Number x = number_field(L, POINT, KEY_X);
    lua_Number y = number_field(L, POINT, KEY_Y);

    if (!lua_equal(L, CONTAINER, SELF)) {
        get_field(L, CONTAINER, KEY_T);
        Rect container = read_rect(L, lua_gettop(L));
        lua_pop(L, 1);
        into_container(&container, &x, &y);
    }
    /* Written as "not below" so that a NaN angle takes the rotated path, as it does in Lua. */
    if (!(fabs(rect.r) < 0.1))
        unrotate(&rect, &x, &y);
    if (!inside(&rect, buffer, x, y))
        return 0;
    lua_pushboolean(L, 1);
    return 1;
}

int luaopen_game_native(lua_State *L)
{
    lua_newtable(L);
    for (int key = 0; key < KEY_COUNT; key++)
        lua_pushstring(L, KEY_NAMES[key]);
    lua_pushcclosure(L, collides_with_point, KEY_COUNT);
    lua_setfield(L, -2, "collides_with_point");
    lua_setglobal(L, "game_native");
    return 0;
}
