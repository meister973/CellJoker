/*
 * DynaText:draw (engine/text.lua) in C. It makes the same graphics calls with the same arguments,
 * in the same order, as the Lua version: the arithmetic keeps Lua's operation order, so every
 * number rounds the same. A frame with G.DEBUG set runs the game's own Lua version, because
 * draw_boundingrect then draws more. The host runner's HARNESS_DRAW_LOG compares whole runs.
 */
#include <math.h>

#include <lua.h>
#include <lauxlib.h>

#include "lua_runtime.h"

enum Key {
    KEY_G, KEY_LOVE, KEY_GRAPHICS, KEY_PREP_DRAW, KEY_DEBUG, KEY_DRAW_HASH, KEY_UNDER_OVERLAY,
    KEY_TILESIZE, KEY_CHILDREN, KEY_PARTICLE_EFFECT, KEY_DRAW, KEY_SHADOW, KEY_STRINGS,
    KEY_FOCUSED_STRING, KEY_W_OFFSET, KEY_H_OFFSET, KEY_TEXT_OFFSET, KEY_FONT, KEY_FONTSCALE,
    KEY_CONFIG, KEY_SPACING, KEY_SHADOW_COLOUR, KEY_COLOURS, KEY_LETTERS, KEY_MIN_CYCLE_TIME,
    KEY_POP_IN, KEY_LETTER, KEY_DIMS, KEY_OFFSET, KEY_R, KEY_SCALE, KEY_SHADOW_PARRALLAX,
    KEY_ARGS, KEY_DRAW_SHADOW_NORM, KEY_PREFIX, KEY_SUFFIX, KEY_COLOUR, KEY_X, KEY_Y,
    KEY_TRANSLATE, KEY_SET_COLOR, KEY_POP,
    KEY_COUNT
};

static const char *const KEY_NAMES[] = {
    "G", "love", "graphics", "prep_draw", "DEBUG", "DRAW_HASH", "under_overlay",
    "TILESIZE", "children", "particle_effect", "draw", "shadow", "strings",
    "focused_string", "W_offset", "H_offset", "text_offset", "font", "FONTSCALE",
    "config", "spacing", "shadow_colour", "colours", "letters", "min_cycle_time",
    "pop_in", "letter", "dims", "offset", "r", "scale", "shadow_parrallax",
    "ARGS", "draw_shadow_norm", "prefix", "suffix", "colour", "x", "y",
    "translate", "setColor", "pop"
};

/* Fails to compile when a key is added to one list and not the other. */
typedef char key_names_match_keys[sizeof KEY_NAMES / sizeof KEY_NAMES[0] == KEY_COUNT ? 1 : -1];

/* The game's own DynaText.draw, the upvalue after the key names. */
#define ORIGINAL lua_upvalueindex(KEY_COUNT + 1)

enum { STACK_NEEDED = 40 };

#define UPVALUE(key) lua_upvalueindex((key) + 1)

/* What one call of draw works with. The Lua version reads these again for each letter, but no
 * call in the draw changes them. */
typedef struct {
    int self, g, gfx, config, string;
    lua_Number fontscale, tilesize, scale;
    lua_Number parallax_x, parallax_y;
} Text;

/* What the draw of one letter reads. */
typedef struct {
    lua_Number dims_x, dims_y, offset_x, offset_y, pop_in;
} Letter;

static void get(lua_State *L, int table, enum Key key)
{
    lua_pushvalue(L, UPVALUE(key));
    lua_gettable(L, table);
}

static int push(lua_State *L, int table, enum Key key)
{
    get(L, table, key);
    return lua_gettop(L);
}

static lua_Number number(lua_State *L, int table, enum Key key)
{
    get(L, table, key);
    if (!lua_isnumber(L, -1))
        luaL_error(L, "DynaText:draw: %s is %s, not a number", KEY_NAMES[key], luaL_typename(L, -1));
    lua_Number value = lua_tonumber(L, -1);
    lua_pop(L, 1);
    return value;
}

static int truthy(lua_State *L, int table, enum Key key)
{
    get(L, table, key);
    int value = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return value;
}

/* table.key.x and table.key.y */
static void pair(lua_State *L, int table, enum Key key, lua_Number *x, lua_Number *y)
{
    int top = lua_gettop(L);
    int inner = push(L, table, key);
    *x = number(L, inner, KEY_X);
    *y = number(L, inner, KEY_Y);
    lua_settop(L, top);
}

static void graphics_numbers(lua_State *L, const Text *t, enum Key key, int count, const lua_Number *values)
{
    get(L, t->gfx, key);
    for (int i = 0; i < count; i++)
        lua_pushnumber(L, values[i]);
    lua_call(L, count, 0);
}

static void translate(lua_State *L, const Text *t, lua_Number x, lua_Number y)
{
    lua_Number move[2] = { x, y };
    graphics_numbers(L, t, KEY_TRANSLATE, 2, move);
}

/* love.graphics.setColor(value) for the value on top of the stack, which it pops. */
static void set_colour_top(lua_State *L, const Text *t)
{
    get(L, t->gfx, KEY_SET_COLOR);
    lua_insert(L, -2);
    lua_call(L, 1, 0);
}

static void graphics_pop(lua_State *L, const Text *t)
{
    get(L, t->gfx, KEY_POP);
    lua_call(L, 0, 0);
}

/* `value*self.font.FONTSCALE/G.TILESIZE` */
static lua_Number to_tiles(const Text *t, lua_Number value)
{
    return value * t->fontscale / t->tilesize;
}

/* prep_draw(self, 1), then the translates to the start of the focused string. */
static void begin_pass(lua_State *L, const Text *t)
{
    get(L, LUA_GLOBALSINDEX, KEY_PREP_DRAW);
    lua_pushvalue(L, t->self);
    lua_pushnumber(L, 1);
    lua_call(L, 2, 0);

    lua_Number offset_x, offset_y;
    pair(L, t->self, KEY_TEXT_OFFSET, &offset_x, &offset_y);
    lua_Number w_offset = number(L, t->string, KEY_W_OFFSET);
    lua_Number h_offset = number(L, t->string, KEY_H_OFFSET);
    translate(L, t, w_offset + to_tiles(t, offset_x), h_offset + to_tiles(t, offset_y));

    int top = lua_gettop(L);
    int spacing = push(L, t->config, KEY_SPACING);
    if (lua_toboolean(L, spacing)) {
        if (!lua_isnumber(L, spacing))
            luaL_error(L, "DynaText:draw: spacing is %s, not a number", luaL_typename(L, spacing));
        translate(L, t, to_tiles(t, lua_tonumber(L, spacing)), 0);
    }
    lua_settop(L, top);
}

/* `self.config.min_cycle_time == 0` */
static int pops_in_at_once(lua_State *L, const Text *t)
{
    get(L, t->config, KEY_MIN_CYCLE_TIME);
    int at_once = lua_type(L, -1) == LUA_TNUMBER && lua_tonumber(L, -1) == 0;
    lua_pop(L, 1);
    return at_once;
}

/* Reads letter.dims, letter.offset and `real_pop_in`. */
static Letter read_letter(lua_State *L, int letter, int at_once)
{
    Letter l;
    pair(L, letter, KEY_DIMS, &l.dims_x, &l.dims_y);
    pair(L, letter, KEY_OFFSET, &l.offset_x, &l.offset_y);
    l.pop_in = at_once ? 1 : number(L, letter, KEY_POP_IN);
    return l;
}

/* love.graphics.draw(letter.letter, x, y, letter.r or 0, sx, sx, ox, oy) */
static void draw_letter(lua_State *L, const Text *t, int letter, lua_Number x, lua_Number y, lua_Number sx,
    const Letter *l)
{
    get(L, t->gfx, KEY_DRAW);
    get(L, letter, KEY_LETTER);
    lua_pushnumber(L, x);
    lua_pushnumber(L, y);
    get(L, letter, KEY_R);
    if (!lua_toboolean(L, -1)) {
        lua_pop(L, 1);
        lua_pushnumber(L, 0);
    }
    lua_pushnumber(L, sx);
    lua_pushnumber(L, sx);
    lua_pushnumber(L, 0.5 * l->dims_x / t->scale);
    lua_pushnumber(L, 0.5 * l->dims_y / t->scale);
    lua_call(L, 8, 0);
    translate(L, t, to_tiles(t, l->dims_x), 0);
}

/* `for k, letter in ipairs(self.strings[self.focused_string].letters)`: pushes letter k, or
 * returns 0 at the end, as ipairs stops at the first nil. */
static int next_letter(lua_State *L, int letters, int k)
{
    lua_rawgeti(L, letters, k);
    if (!lua_isnil(L, -1))
        return lua_gettop(L);
    lua_pop(L, 1);
    return 0;
}

/* love.graphics.setColor(self.config.shadow_colour), or the black of the default shadow. */
static void set_shadow_colour(lua_State *L, const Text *t)
{
    get(L, t->config, KEY_SHADOW_COLOUR);
    if (lua_toboolean(L, -1)) {
        set_colour_top(L, t);
        return;
    }
    lua_pop(L, 1);
    /* `self.colours[1][4]`, read with gettable as Lua does. */
    int top = lua_gettop(L);
    int colours = push(L, t->self, KEY_COLOURS);
    lua_pushinteger(L, 1);
    lua_gettable(L, colours);
    lua_pushinteger(L, 4);
    lua_gettable(L, -2);
    if (!lua_isnumber(L, -1))
        luaL_error(L, "DynaText:draw: colours[1][4] is %s, not a number", luaL_typename(L, -1));
    lua_Number black[4] = { 0, 0, 0, 0.3 * lua_tonumber(L, -1) };
    lua_settop(L, top);
    graphics_numbers(L, t, KEY_SET_COLOR, 4, black);
}

/* The shadow pass: each letter in the shadow colour, moved by the shadow parallax. */
static void draw_shadow(lua_State *L, const Text *t)
{
    begin_pass(L, t);
    set_shadow_colour(L, t);

    int top = lua_gettop(L);
    int at_once = pops_in_at_once(L, t);
    int letters = push(L, t->string, KEY_LETTERS);
    for (int k = 1;; k++) {
        int letter = next_letter(L, letters, k);
        if (!letter)
            break;
        Letter l = read_letter(L, letter, at_once);
        lua_Number x = 0.5 * (l.dims_x - l.offset_x) * t->fontscale / t->tilesize
            - t->parallax_x * t->scale / (t->tilesize);
        lua_Number y = 0.5 * (l.dims_y) * t->fontscale / t->tilesize - t->parallax_y * t->scale / (t->tilesize);
        lua_Number sx = l.pop_in * t->scale * t->fontscale / t->tilesize;
        draw_letter(L, t, letter, x, y, sx, &l);
        lua_settop(L, letters);
    }
    lua_settop(L, top);
    graphics_pop(L, t);
}

/* `self.ARGS.draw_shadow_norm = self.ARGS.draw_shadow_norm or {}` and its x and y, which the
 * main pass adds to each letter. */
static void shadow_norm(lua_State *L, const Text *t, lua_Number *x, lua_Number *y)
{
    int top = lua_gettop(L);
    int args = push(L, t->self, KEY_ARGS);
    int norm = push(L, args, KEY_DRAW_SHADOW_NORM);
    if (!lua_toboolean(L, norm)) {
        lua_pop(L, 1);
        lua_newtable(L);
    }
    lua_pushvalue(L, UPVALUE(KEY_DRAW_SHADOW_NORM));
    lua_pushvalue(L, norm);
    lua_settable(L, args);

    lua_Number px = t->parallax_x, py = t->parallax_y;
    *x = px / sqrt(py * py + px * px) * t->fontscale / t->tilesize;
    *y = py / sqrt(py * py + px * px) * t->fontscale / t->tilesize;
    lua_pushvalue(L, UPVALUE(KEY_X));
    lua_pushnumber(L, *x);
    lua_settable(L, norm);
    lua_pushvalue(L, UPVALUE(KEY_Y));
    lua_pushnumber(L, *y);
    lua_settable(L, norm);
    lua_settop(L, top);
}

/* `letter.prefix or letter.suffix or letter.colour or self.colours[k%#self.colours + 1]` */
static void push_letter_colour(lua_State *L, const Text *t, int letter, int k)
{
    static const enum Key OWN[] = { KEY_PREFIX, KEY_SUFFIX, KEY_COLOUR };
    for (int i = 0; i < 3; i++) {
        get(L, letter, OWN[i]);
        if (lua_toboolean(L, -1))
            return;
        lua_pop(L, 1);
    }
    int colours = push(L, t->self, KEY_COLOURS);
    lua_Number count = (lua_Number)lua_objlen(L, colours);
    /* Lua 5.1's a % b */
    lua_Number index = k - floor(k / count) * count + 1;
    lua_pushnumber(L, index);
    lua_gettable(L, colours);
    lua_remove(L, colours);
}

/* The main pass: each letter in its colour, with the letter's own offset and scale. */
static void draw_letters(lua_State *L, const Text *t)
{
    begin_pass(L, t);
    lua_Number norm_x, norm_y;
    shadow_norm(L, t, &norm_x, &norm_y);

    int top = lua_gettop(L);
    int at_once = pops_in_at_once(L, t);
    int letters = push(L, t->string, KEY_LETTERS);
    for (int k = 1;; k++) {
        int letter = next_letter(L, letters, k);
        if (!letter)
            break;
        Letter l = read_letter(L, letter, at_once);
        push_letter_colour(L, t, letter, k);
        set_colour_top(L, t);
        lua_Number letter_scale = number(L, letter, KEY_SCALE);
        lua_Number x = 0.5 * (l.dims_x - l.offset_x) * t->fontscale / t->tilesize + norm_x;
        lua_Number y = 0.5 * (l.dims_y - l.offset_y) * t->fontscale / t->tilesize + norm_y;
        lua_Number sx = l.pop_in * letter_scale * t->scale * t->fontscale / t->tilesize;
        draw_letter(L, t, letter, x, y, sx, &l);
        lua_settop(L, letters);
    }
    lua_settop(L, top);
    graphics_pop(L, t);
}

/* `if self.children.particle_effect then self.children.particle_effect:draw() end` */
static void draw_particles(lua_State *L, const Text *t)
{
    int top = lua_gettop(L);
    int effect = push(L, push(L, t->self, KEY_CHILDREN), KEY_PARTICLE_EFFECT);
    if (lua_toboolean(L, effect)) {
        get(L, effect, KEY_DRAW);
        lua_pushvalue(L, effect);
        lua_call(L, 1, 0);
    }
    lua_settop(L, top);
}

/* add_to_drawhash(self), then Node:draw_boundingrect without G.DEBUG. */
static void finish(lua_State *L, const Text *t)
{
    int top = lua_gettop(L);
    int hash = push(L, t->g, KEY_DRAW_HASH);
    lua_pushinteger(L, (lua_Integer)lua_objlen(L, hash) + 1);
    lua_pushvalue(L, t->self);
    lua_settable(L, hash);
    lua_settop(L, top);

    lua_pushvalue(L, UPVALUE(KEY_UNDER_OVERLAY));
    get(L, t->g, KEY_UNDER_OVERLAY);
    lua_settable(L, t->self);
}

/* Reads the fields that the Lua version reads for every letter. */
static void read_text(lua_State *L, Text *t)
{
    t->gfx = push(L, push(L, LUA_GLOBALSINDEX, KEY_LOVE), KEY_GRAPHICS);
    t->config = push(L, t->self, KEY_CONFIG);
    int strings = push(L, t->self, KEY_STRINGS);
    get(L, t->self, KEY_FOCUSED_STRING);
    lua_gettable(L, strings);
    t->string = lua_gettop(L);
    int font = push(L, t->self, KEY_FONT);
    t->fontscale = number(L, font, KEY_FONTSCALE);
    t->tilesize = number(L, t->g, KEY_TILESIZE);
    t->scale = number(L, t->self, KEY_SCALE);
    pair(L, t->self, KEY_SHADOW_PARRALLAX, &t->parallax_x, &t->parallax_y);
}

/* DynaText:draw() */
static int draw(lua_State *L)
{
    lua_settop(L, 1);
    luaL_checkstack(L, STACK_NEEDED, "DynaText:draw");
    Text t = { 0 };
    t.self = 1;
    t.g = push(L, LUA_GLOBALSINDEX, KEY_G);
    if (truthy(L, t.g, KEY_DEBUG)) {
        lua_pushvalue(L, ORIGINAL);
        lua_pushvalue(L, t.self);
        lua_call(L, 1, 0);
        return 0;
    }

    draw_particles(L, &t);
    read_text(L, &t);
    if (truthy(L, t.self, KEY_SHADOW))
        draw_shadow(L, &t);
    draw_letters(L, &t);
    finish(L, &t);
    return 0;
}

/* game_native.text_draw(original): the C DynaText.draw, which calls `original`, the game's
 * DynaText.draw, when G.DEBUG is set. */
static int make_draw(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TFUNCTION);
    luaL_checkstack(L, KEY_COUNT + 2, "text_draw");
    for (int key = 0; key < KEY_COUNT; key++)
        lua_pushstring(L, KEY_NAMES[key]);
    lua_pushvalue(L, 1);
    lua_pushcclosure(L, draw, KEY_COUNT + 1);
    return 1;
}

void game_native_register_text(lua_State *L)
{
    lua_pushcfunction(L, make_draw);
    lua_setfield(L, -2, "text_draw");
}
