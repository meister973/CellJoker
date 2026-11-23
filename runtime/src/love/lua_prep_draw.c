/*
 * The game's prep_draw (functions/misc_functions.lua) in C. Every sprite, text and UI element
 * calls it before it draws, and in Lua it is about 9% of a frame's interpreter work. It reads
 * the same fields and makes the same transform calls with the same float conversions, so it
 * builds the transform the Lua version builds, bit for bit. tests/prep_draw checks this.
 */
#include <lua.h>
#include <lauxlib.h>

#include "lua_graphics.h"

/* The field names, made once and kept as upvalues: lua_getfield would hash and intern its key
 * string on every call, and this function reads about fifteen fields each time it runs. */
enum Key { KEY_VT, KEY_LAYERED_PARALLAX, KEY_PARENT, KEY_JUICE, KEY_G, KEY_TILESCALE, KEY_TILESIZE,
    KEY_X, KEY_Y, KEY_W, KEY_H, KEY_R, KEY_SCALE, KEY_COUNT };

static const char *const KEY_NAMES[KEY_COUNT] = { "VT", "layered_parallax", "parent", "juice", "G",
    "TILESCALE", "TILESIZE", "x", "y", "w", "h", "r", "scale" };

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
        luaL_error(L, "prep_draw: %s is %s, not a number", KEY_NAMES[key], luaL_typename(L, -1));
    lua_Number value = lua_tonumber(L, -1);
    lua_pop(L, 1);
    return value;
}

static lua_Number number_field(lua_State *L, int table, enum Key key)
{
    get_field(L, table, key);
    return pop_number(L, key);
}

/* Lua's `value and value[key]`: pushes the field and returns 1 when it is truthy, and
 * otherwise pushes nothing and returns 0. */
static int push_truthy_field(lua_State *L, int value, enum Key key)
{
    if (!lua_toboolean(L, value))
        return 0;
    get_field(L, value, key);
    if (lua_toboolean(L, -1))
        return 1;
    lua_pop(L, 1);
    return 0;
}

/* `value and value[key] or 0` */
static lua_Number field_or_zero(lua_State *L, int value, enum Key key)
{
    if (!push_truthy_field(L, value, key))
        return 0;
    return pop_number(L, key);
}

/* `(own and own[axis]) or (parent and parent[axis]) or 0`, for the layered parallax tables. */
static lua_Number parallax(lua_State *L, int own, int parent, enum Key axis)
{
    if (push_truthy_field(L, own, axis) || push_truthy_field(L, parent, axis))
        return pop_number(L, axis);
    return 0;
}

/* prep_draw(moveable, scale, rotate, offset) */
static int prep_draw(lua_State *L)
{
    enum { MOVEABLE = 1, SCALE, ROTATE, OFFSET, VT, OWN_PARALLAX, PARENT, PARENT_PARALLAX, GAME };

    lua_settop(L, OFFSET);
    graphics_push(L);

    get_field(L, MOVEABLE, KEY_VT);
    get_field(L, MOVEABLE, KEY_LAYERED_PARALLAX);
    get_field(L, MOVEABLE, KEY_PARENT);
    if (lua_toboolean(L, PARENT))
        get_field(L, PARENT, KEY_LAYERED_PARALLAX);
    else
        lua_pushnil(L);
    get_field(L, LUA_GLOBALSINDEX, KEY_G);

    lua_Number tile_scale = number_field(L, GAME, KEY_TILESCALE);
    lua_Number tile_size = number_field(L, GAME, KEY_TILESIZE);
    f32 tile = (f32)(tile_scale * tile_size);
    graphics_scale(tile, tile);

    lua_Number x = number_field(L, VT, KEY_X);
    lua_Number y = number_field(L, VT, KEY_Y);
    lua_Number w = number_field(L, VT, KEY_W);
    lua_Number h = number_field(L, VT, KEY_H);
    lua_Number center_x = x + w / 2 + field_or_zero(L, OFFSET, KEY_X) + parallax(L, OWN_PARALLAX, PARENT_PARALLAX, KEY_X);
    lua_Number center_y = y + h / 2 + field_or_zero(L, OFFSET, KEY_Y) + parallax(L, OWN_PARALLAX, PARENT_PARALLAX, KEY_Y);
    graphics_translate((f32)center_x, (f32)center_y);

    lua_Number r = number_field(L, VT, KEY_R);
    get_field(L, MOVEABLE, KEY_JUICE);
    int juice = lua_toboolean(L, -1);
    lua_pop(L, 1);
    int rotate = lua_toboolean(L, ROTATE);
    if (r != 0 || juice || rotate)
        graphics_rotate((f32)(r + (rotate ? luaL_checknumber(L, ROTATE) : 0)));

    lua_Number scale = luaL_checknumber(L, SCALE);
    lua_Number vt_scale = number_field(L, VT, KEY_SCALE);
    graphics_translate((f32)(-scale * w * vt_scale / 2), (f32)(-scale * h * vt_scale / 2));
    f32 size = (f32)(vt_scale * scale);
    graphics_scale(size, size);
    return 0;
}

void lua_graphics_register_prep_draw(lua_State *L)
{
    for (int key = 0; key < KEY_COUNT; key++)
        lua_pushstring(L, KEY_NAMES[key]);
    lua_pushcclosure(L, prep_draw, KEY_COUNT);
    lua_setfield(L, -2, "prepDraw");
}
