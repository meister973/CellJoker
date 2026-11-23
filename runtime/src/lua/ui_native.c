/*
 * UIElement:draw_self (engine/ui.lua) in C, for the elements that most frames draw: text, boxes,
 * rows, columns and objects without focus. An element with an outline, focus, a chosen triangle,
 * a button delay or a progress bar, or a frame with G.DEBUG set, runs the game's own Lua version.
 * The graphics calls, prep_draw, darken's arithmetic and the cache of draw_pixellated_rect match
 * the Lua version. A cache miss calls the game's draw_pixellated_rect, which builds the vertices.
 * The host runner's HARNESS_DRAW_LOG compares the graphics calls of whole runs.
 */
#include <lua.h>
#include <lauxlib.h>

#include "lua_runtime.h"

/* Lua 5.1's math.pi, so `-math.pi/2` rounds as it does in Lua. */
static const lua_Number PI = 3.14159265358979323846;

enum Key {
    KEY_G, KEY_LOVE, KEY_GRAPHICS, KEY_PREP_DRAW, KEY_STATES, KEY_CONFIG, KEY_VISIBLE,
    KEY_FORCE_FOCUS, KEY_FORCE_COLLISION, KEY_BUTTON_UIE, KEY_BUTTON, KEY_COLLIDE, KEY_CAN,
    KEY_DRAW_HASH, KEY_OUTLINE, KEY_OUTLINE_COLOUR, KEY_FOCUS, KEY_IS, KEY_CHOSEN, KEY_DEBUG,
    KEY_COLOUR, KEY_UIT, KEY_UIT_T, KEY_UIT_B, KEY_UIT_C, KEY_UIT_R, KEY_UIT_ROOT, KEY_UIT_O,
    KEY_OBJECT, KEY_FOCUS_WITH_OBJECT, KEY_BUTTON_DELAY, KEY_PROGRESS_BAR, KEY_LAYERED_PARALLAX,
    KEY_PARENT, KEY_UIBOX, KEY_X, KEY_Y, KEY_W, KEY_H, KEY_SHADOW, KEY_SHADOW_PARRALLAX,
    KEY_TILESIZE, KEY_LAST_CLICKED, KEY_TIMERS, KEY_REAL, KEY_HOVER, KEY_DRAG, KEY_CONTROLLER,
    KEY_IS_CURSOR_DOWN, KEY_BUTTON_DIST, KEY_SCALE, KEY_ARGS, KEY_TEXT_PARALLAX, KEY_SX, KEY_SY,
    KEY_LANG, KEY_FONT, KEY_FONTSCALE, KEY_SETTINGS, KEY_GRAPHICS_SETTINGS, KEY_SHADOWS,
    KEY_VERT, KEY_VT, KEY_TEXT_OFFSET, KEY_SQUISH, KEY_TEXT_DRAWABLE, KEY_C, KEY_UI,
    KEY_TEXT_INACTIVE, KEY_HOVER_COLOUR, KEY_SHADOW_COLOUR, KEY_R, KEY_EMBOSS, KEY_BUTTON_COLOURS,
    KEY_PIXELLATED_RECT, KEY_VERTICES, KEY_PARALLAX, KEY_SW, KEY_SH, KEY_PROGRESS,
    KEY_DRAW_PIXELLATED_RECT, KEY_OBJECT_FOCUS_TIMER, KEY_DRAW, KEY_FOCUS_TIMER,
    KEY_UNDER_OVERLAY, KEY_POP, KEY_TRANSLATE, KEY_ROTATE, KEY_SET_COLOR,
    KEY_RECTANGLE, KEY_POLYGON, VALUE_ON, VALUE_FILL, VALUE_SHADOW_TYPE, VALUE_EMBOSS_TYPE,
    KEY_COUNT
};

static const char *const KEY_NAMES[] = {
    "G", "love", "graphics", "prep_draw", "states", "config", "visible",
    "force_focus", "force_collision", "button_UIE", "button", "collide", "can",
    "DRAW_HASH", "outline", "outline_colour", "focus", "is", "chosen", "DEBUG",
    "colour", "UIT", "T", "B", "C", "R", "ROOT", "O",
    "object", "focus_with_object", "button_delay", "progress_bar", "layered_parallax",
    "parent", "UIBox", "x", "y", "w", "h", "shadow", "shadow_parrallax",
    "TILESIZE", "last_clicked", "TIMERS", "REAL", "hover", "drag", "CONTROLLER",
    "is_cursor_down", "button_dist", "scale", "ARGS", "text_parallax", "sx", "sy",
    "lang", "font", "FONTSCALE", "SETTINGS", "GRAPHICS", "shadows",
    "vert", "VT", "TEXT_OFFSET", "squish", "text_drawable", "C", "UI",
    "TEXT_INACTIVE", "HOVER", "shadow_colour", "r", "emboss", "button_colours",
    "pixellated_rect", "vertices", "parallax", "sw", "sh", "progress",
    "draw_pixellated_rect", "object_focus_timer", "draw", "focus_timer",
    "under_overlay", "pop", "translate", "rotate", "setColor",
    "rectangle", "polygon", "On", "fill", "shadow", "emboss"
};

/* Fails to compile when a key is added to one list and not the other. */
typedef char key_names_match_keys[sizeof KEY_NAMES / sizeof KEY_NAMES[0] == KEY_COUNT ? 1 : -1];

/* The game's own UIElement.draw_self, the upvalue after the key names. */
#define ORIGINAL lua_upvalueindex(KEY_COUNT + 1)

enum { STACK_NEEDED = 40 };

#define UPVALUE(key) lua_upvalueindex((key) + 1)

/* What one call of draw_self works with, as absolute stack indexes and numbers. */
typedef struct {
    int self, g, gfx, config, states;
    lua_Number tilesize;
    lua_Number parallax_dist;
    int button_active, button_being_pressed;
} Frame;

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

static lua_Number to_number(lua_State *L, int index, enum Key key)
{
    if (!lua_isnumber(L, index))
        luaL_error(L, "UIElement:draw_self: %s is %s, not a number", KEY_NAMES[key], luaL_typename(L, index));
    return lua_tonumber(L, index);
}

static lua_Number number(lua_State *L, int table, enum Key key)
{
    get(L, table, key);
    lua_Number value = to_number(L, -1, key);
    lua_pop(L, 1);
    return value;
}

/* table[1..4] read as `colour[i]` */
static lua_Number element(lua_State *L, int table, int i)
{
    lua_pushinteger(L, i);
    lua_gettable(L, table);
    lua_Number value = to_number(L, -1, KEY_COLOUR);
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

/* table.key.inner_key */
static int nested_truthy(lua_State *L, int table, enum Key key, enum Key inner_key)
{
    int top = lua_gettop(L);
    int inner = push(L, table, key);
    int value = truthy(L, inner, inner_key);
    lua_settop(L, top);
    return value;
}

/* table.key.is */
static int is_set(lua_State *L, int table, enum Key key)
{
    return nested_truthy(L, table, key, KEY_IS);
}

/* `a == b` for table.key and other.key */
static int fields_equal(lua_State *L, int table, enum Key key, int other, enum Key other_key)
{
    get(L, table, key);
    get(L, other, other_key);
    int same = lua_equal(L, -2, -1);
    lua_pop(L, 2);
    return same;
}

/* `table.key > limit` */
static int above(lua_State *L, int table, enum Key key, lua_Number limit)
{
    return number(L, table, key) > limit;
}

static lua_Number colour_alpha(lua_State *L, int config, enum Key key)
{
    int top = lua_gettop(L);
    int colour = push(L, config, key);
    lua_Number alpha = element(L, colour, 4);
    lua_settop(L, top);
    return alpha;
}

/* Starts love.graphics.<key>(...): pushes the function. The caller pushes the arguments and calls. */
static void begin(lua_State *L, const Frame *f, enum Key key)
{
    get(L, f->gfx, key);
}

static void graphics_numbers(lua_State *L, const Frame *f, enum Key key, int count, const lua_Number *values)
{
    begin(L, f, key);
    for (int i = 0; i < count; i++)
        lua_pushnumber(L, values[i]);
    lua_call(L, count, 0);
}

static void graphics_scale(lua_State *L, const Frame *f, lua_Number scale)
{
    graphics_numbers(L, f, KEY_SCALE, 1, &scale);
}

static void graphics_pop(lua_State *L, const Frame *f)
{
    begin(L, f, KEY_POP);
    lua_call(L, 0, 0);
}

/* love.graphics.setColor(value) for a colour table at `index`. */
static void set_colour_table(lua_State *L, const Frame *f, int index)
{
    begin(L, f, KEY_SET_COLOR);
    lua_pushvalue(L, index);
    lua_call(L, 1, 0);
}

static void prep_draw(lua_State *L, const Frame *f, lua_Number scale)
{
    get(L, LUA_GLOBALSINDEX, KEY_PREP_DRAW);
    lua_pushvalue(L, f->self);
    lua_pushnumber(L, scale);
    lua_call(L, 2, 0);
}

/* `if self.config.vert then love.graphics.translate(0,self.VT.h); love.graphics.rotate(-math.pi/2) end` */
static void turn_if_vertical(lua_State *L, const Frame *f)
{
    if (!truthy(L, f->config, KEY_VERT))
        return;
    int top = lua_gettop(L);
    int vt = push(L, f->self, KEY_VT);
    lua_Number move[2] = { 0, number(L, vt, KEY_H) };
    lua_Number angle = -PI / 2;
    lua_settop(L, top);
    graphics_numbers(L, f, KEY_TRANSLATE, 2, move);
    graphics_numbers(L, f, KEY_ROTATE, 1, &angle);
}

static void add_to_drawhash(lua_State *L, int g, int self)
{
    int top = lua_gettop(L);
    int hash = push(L, g, KEY_DRAW_HASH);
    lua_pushinteger(L, (lua_Integer)lua_objlen(L, hash) + 1);
    lua_pushvalue(L, self);
    lua_settable(L, hash);
    lua_settop(L, top);
}

/* The parts that run the game's Lua version: they are rare, and they call more game helpers. */
static int needs_lua(lua_State *L, const Frame *f)
{
    if (truthy(L, f->config, KEY_OUTLINE) && colour_alpha(L, f->config, KEY_OUTLINE_COLOUR) > 0.01)
        return 1;
    if (is_set(L, f->states, KEY_FOCUS) || truthy(L, f->config, KEY_CHOSEN) || truthy(L, f->g, KEY_DEBUG))
        return 1;
    if (!(colour_alpha(L, f->config, KEY_COLOUR) > 0.01))
        return 0;

    int top = lua_gettop(L);
    int uit = push(L, f->g, KEY_UIT);
    int slow = 0;
    if (fields_equal(L, f->self, KEY_UIT, uit, KEY_UIT_O) && truthy(L, f->config, KEY_OBJECT)) {
        int object = push(L, f->config, KEY_OBJECT);
        slow = truthy(L, f->config, KEY_FOCUS_WITH_OBJECT) && is_set(L, push(L, object, KEY_STATES), KEY_FOCUS);
    } else if (truthy(L, f->config, KEY_BUTTON_DELAY) || truthy(L, f->config, KEY_PROGRESS_BAR)) {
        slow = 1;
    }
    lua_settop(L, top);
    return slow;
}

/* `(self.parent and self.parent ~= self.UIBox and self.parent.layered_parallax.<axis> or 0)
 *  + (self.config.shadow and 0.4*self.shadow_parrallax.<axis> or 0)/G.TILESIZE` */
static lua_Number button_parallax(lua_State *L, const Frame *f, enum Key axis)
{
    int top = lua_gettop(L);
    int parent = push(L, f->self, KEY_PARENT);
    lua_Number from_parent = 0;

    if (lua_toboolean(L, parent)) {
        get(L, f->self, KEY_UIBOX);
        int other = !lua_equal(L, parent, -1);
        lua_pop(L, 1);
        if (other) {
            int layered = push(L, parent, KEY_LAYERED_PARALLAX);
            get(L, layered, axis);
            if (lua_toboolean(L, -1))
                from_parent = to_number(L, -1, axis);
        }
    }
    lua_Number shadow = 0;
    if (truthy(L, f->config, KEY_SHADOW))
        shadow = 0.4 * number(L, push(L, f->self, KEY_SHADOW_PARRALLAX), axis);
    lua_settop(L, top);
    return from_parent + shadow / f->tilesize;
}

/* `(self.last_clicked and self.last_clicked > G.TIMERS.REAL - 0.1)` for `element` */
static int clicked_recently(lua_State *L, const Frame *f, int element)
{
    int top = lua_gettop(L);
    int last = push(L, element, KEY_LAST_CLICKED);
    int recent = 0;

    if (lua_toboolean(L, last)) {
        lua_Number clicked = to_number(L, last, KEY_LAST_CLICKED);
        recent = clicked > number(L, push(L, f->g, KEY_TIMERS), KEY_REAL) - 0.1;
    }
    lua_settop(L, top);
    return recent;
}

static int being_pressed(lua_State *L, const Frame *f)
{
    if (!truthy(L, f->config, KEY_BUTTON))
        return 0;
    if (clicked_recently(L, f, f->self))
        return 1;
    if (!(is_set(L, f->states, KEY_HOVER) || is_set(L, f->states, KEY_DRAG)))
        return 0;
    int top = lua_gettop(L);
    int pressed = truthy(L, push(L, f->g, KEY_CONTROLLER), KEY_IS_CURSOR_DOWN);
    lua_settop(L, top);
    return pressed;
}

/* The `self.config.button or self.config.button_UIE` block. */
static void move_button(lua_State *L, Frame *f)
{
    if (!(truthy(L, f->config, KEY_BUTTON) || truthy(L, f->config, KEY_BUTTON_UIE)))
        return;
    int top = lua_gettop(L);
    int layered = push(L, f->self, KEY_LAYERED_PARALLAX);
    lua_Number x = button_parallax(L, f, KEY_X);
    lua_pushvalue(L, UPVALUE(KEY_X));
    lua_pushnumber(L, x);
    lua_settable(L, layered);
    lua_Number y = button_parallax(L, f, KEY_Y);
    lua_pushvalue(L, UPVALUE(KEY_Y));
    lua_pushnumber(L, y);
    lua_settable(L, layered);

    if (being_pressed(L, f)) {
        int shadow = push(L, f->self, KEY_SHADOW_PARRALLAX);
        get(L, f->config, KEY_BUTTON_DIST);
        lua_Number dist = lua_toboolean(L, -1) ? to_number(L, -1, KEY_BUTTON_DIST) : 1;
        lua_pop(L, 1);
        x = x - f->parallax_dist * number(L, shadow, KEY_X) / f->tilesize * dist;
        lua_pushvalue(L, UPVALUE(KEY_X));
        lua_pushnumber(L, x);
        lua_settable(L, layered);
        y = y - f->parallax_dist * number(L, shadow, KEY_Y) / f->tilesize * dist;
        lua_pushvalue(L, UPVALUE(KEY_Y));
        lua_pushnumber(L, y);
        lua_settable(L, layered);
        f->parallax_dist = 0;
        f->button_being_pressed = 1;
    }

    int button_uie = push(L, f->config, KEY_BUTTON_UIE);
    if (lua_toboolean(L, button_uie) && !truthy(L, push(L, button_uie, KEY_CONFIG), KEY_BUTTON))
        f->button_active = 0;
    lua_settop(L, top);
}

/* The numbers that the text branch draws with. */
typedef struct {
    lua_Number scale, fontscale, squish, offset_x, offset_y, sx, sy;
    int vert;
} Text;

static void draw_text_at(lua_State *L, const Frame *f, const Text *t, lua_Number x, lua_Number y)
{
    begin(L, f, KEY_DRAW);
    get(L, f->config, KEY_TEXT_DRAWABLE);
    lua_pushnumber(L, x);
    lua_pushnumber(L, y);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, t->scale * t->squish * t->fontscale / f->tilesize);
    lua_pushnumber(L, t->scale * t->fontscale / f->tilesize);
    lua_call(L, 6, 0);
}

static int shadows_on(lua_State *L, const Frame *f)
{
    int top = lua_gettop(L);
    int settings = push(L, f->g, KEY_SETTINGS);
    int graphics = push(L, settings, KEY_GRAPHICS_SETTINGS);
    get(L, graphics, KEY_SHADOWS);
    int on = lua_rawequal(L, -1, UPVALUE(VALUE_ON));
    lua_settop(L, top);
    return on;
}

static void draw_text_shadow(lua_State *L, const Frame *f, const Text *t, lua_Number colour_alpha_value)
{
    int button_uie = truthy(L, f->config, KEY_BUTTON_UIE);
    int shadow = truthy(L, f->config, KEY_SHADOW);

    if (!((button_uie && f->button_active) || (!button_uie && shadow && shadows_on(L, f))))
        return;
    prep_draw(L, f, 0.97);
    turn_if_vertical(L, f);
    if ((shadow || (button_uie && f->button_active)) && shadows_on(L, f)) {
        lua_Number black[4] = { 0, 0, 0, 0.3 * colour_alpha_value };
        graphics_numbers(L, f, KEY_SET_COLOR, 4, black);
        lua_Number x = (t->offset_x + (t->vert ? -t->sy : t->sx)) * t->scale * t->fontscale / f->tilesize;
        lua_Number y = (t->offset_y + (t->vert ? t->sx : t->sy)) * t->scale * t->fontscale / f->tilesize;
        draw_text_at(L, f, t, x, y);
    }
    graphics_pop(L, f);
}

/* The `self.UIT == G.UIT.T and self.config.scale` branch. */
static void draw_text(lua_State *L, const Frame *f, lua_Number colour_alpha_value)
{
    int top = lua_gettop(L);
    int shadow = push(L, f->self, KEY_SHADOW_PARRALLAX);
    int lang = push(L, f->config, KEY_LANG);
    int font = push(L, lang, KEY_FONT);
    Text t;
    t.scale = number(L, f->config, KEY_SCALE);
    t.fontscale = number(L, font, KEY_FONTSCALE);

    int args = push(L, f->self, KEY_ARGS);
    int parallax = push(L, args, KEY_TEXT_PARALLAX);
    if (!lua_toboolean(L, parallax)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, UPVALUE(KEY_TEXT_PARALLAX));
        lua_pushvalue(L, -2);
        lua_settable(L, args);
    }
    t.sx = -number(L, shadow, KEY_X) * 0.5 / (t.scale * t.fontscale);
    lua_pushvalue(L, UPVALUE(KEY_SX));
    lua_pushnumber(L, t.sx);
    lua_settable(L, parallax);
    t.sy = -number(L, shadow, KEY_Y) * 0.5 / (t.scale * t.fontscale);
    lua_pushvalue(L, UPVALUE(KEY_SY));
    lua_pushnumber(L, t.sy);
    lua_settable(L, parallax);

    int offset = push(L, font, KEY_TEXT_OFFSET);
    t.offset_x = number(L, offset, KEY_X);
    t.offset_y = number(L, offset, KEY_Y);
    t.squish = number(L, font, KEY_SQUISH);
    t.vert = truthy(L, f->config, KEY_VERT);

    draw_text_shadow(L, f, &t, colour_alpha_value);

    prep_draw(L, f, 1);
    turn_if_vertical(L, f);
    if (!f->button_active) {
        int c = push(L, f->g, KEY_C);
        int ui = push(L, c, KEY_UI);
        set_colour_table(L, f, push(L, ui, KEY_TEXT_INACTIVE));
    } else {
        set_colour_table(L, f, push(L, f->config, KEY_COLOUR));
    }
    draw_text_at(L, f, &t, t.offset_x * t.scale * t.fontscale / f->tilesize,
        t.offset_y * t.scale * t.fontscale / f->tilesize);
    graphics_pop(L, f);
    lua_settop(L, top);
}

/* UIElement:draw_pixellated_rect(type, parallax_dist[, emboss]). The cached vertices are drawn
 * here; a miss calls the game's version, which rebuilds them. `emboss` is 0 for no argument. */
static int cache_hit(lua_State *L, const Frame *f, int rect, enum Key type, lua_Number parallax)
{
    int top = lua_gettop(L);
    int hit = 0;

    if (!lua_toboolean(L, rect))
        goto done;
    int vertices = push(L, push(L, rect, type), KEY_VERTICES);
    if (!((lua_Number)lua_objlen(L, vertices) >= 1))
        goto done;
    lua_pushnumber(L, parallax);
    get(L, rect, KEY_PARALLAX);
    if (!lua_equal(L, -2, -1))
        goto done;
    int vt = push(L, f->self, KEY_VT);
    int shadow = push(L, f->self, KEY_SHADOW_PARRALLAX);
    if (!fields_equal(L, rect, KEY_W, vt, KEY_W) || !fields_equal(L, rect, KEY_H, vt, KEY_H))
        goto done;
    if (!fields_equal(L, rect, KEY_SW, shadow, KEY_X) || !fields_equal(L, rect, KEY_SH, shadow, KEY_Y))
        goto done;
    lua_pushnumber(L, 1);
    get(L, rect, KEY_PROGRESS);
    hit = lua_equal(L, -2, -1);
done:
    lua_settop(L, top);
    return hit;
}

static void pixellated_rect(lua_State *L, const Frame *f, enum Key type, int emboss)
{
    int top = lua_gettop(L);
    int rect = push(L, f->self, KEY_PIXELLATED_RECT);

    if (cache_hit(L, f, rect, type, f->parallax_dist)) {
        begin(L, f, KEY_POLYGON);
        lua_pushvalue(L, UPVALUE(VALUE_FILL));
        push(L, push(L, rect, type), KEY_VERTICES);
        lua_remove(L, -2);
        lua_call(L, 2, 0);
        lua_settop(L, top);
        return;
    }
    get(L, f->self, KEY_DRAW_PIXELLATED_RECT);
    lua_pushvalue(L, f->self);
    lua_pushvalue(L, UPVALUE(type));
    lua_pushnumber(L, f->parallax_dist);
    if (emboss) {
        lua_pushvalue(L, emboss);
        lua_call(L, 4, 0);
    } else {
        lua_call(L, 3, 0);
    }
    lua_settop(L, top);
}

/* `self.config.r and self.VT.w > 0.01` */
static int rounded(lua_State *L, const Frame *f)
{
    if (!truthy(L, f->config, KEY_R))
        return 0;
    int top = lua_gettop(L);
    int rounded_rect = above(L, push(L, f->self, KEY_VT), KEY_W, 0.01);
    lua_settop(L, top);
    return rounded_rect;
}

/* love.graphics.rectangle('fill', x, y, self.VT.w*G.TILESIZE, self.VT.h*G.TILESIZE) */
static void plain_rect(lua_State *L, const Frame *f, lua_Number x, lua_Number y)
{
    int top = lua_gettop(L);
    int vt = push(L, f->self, KEY_VT);
    lua_Number w = number(L, vt, KEY_W) * f->tilesize;
    lua_Number h = number(L, vt, KEY_H) * f->tilesize;
    begin(L, f, KEY_RECTANGLE);
    lua_pushvalue(L, UPVALUE(VALUE_FILL));
    lua_pushnumber(L, x);
    lua_pushnumber(L, y);
    lua_pushnumber(L, w);
    lua_pushnumber(L, h);
    lua_call(L, 5, 0);
    lua_settop(L, top);
}

static void draw_box_shadow(lua_State *L, const Frame *f, lua_Number colour_alpha_value)
{
    if (!(truthy(L, f->config, KEY_SHADOW) && shadows_on(L, f)))
        return;
    int top = lua_gettop(L);
    graphics_scale(L, f, 0.98);
    int shadow_colour = push(L, f->config, KEY_SHADOW_COLOUR);
    if (lua_toboolean(L, shadow_colour)) {
        set_colour_table(L, f, shadow_colour);
    } else {
        lua_Number black[4] = { 0, 0, 0, 0.3 * colour_alpha_value };
        graphics_numbers(L, f, KEY_SET_COLOR, 4, black);
    }
    if (rounded(L, f)) {
        pixellated_rect(L, f, VALUE_SHADOW_TYPE, 0);
    } else {
        int shadow = push(L, f->self, KEY_SHADOW_PARRALLAX);
        plain_rect(L, f, -number(L, shadow, KEY_X) * f->parallax_dist, -number(L, shadow, KEY_Y) * f->parallax_dist);
    }
    graphics_scale(L, f, 1 / 0.98);
    lua_settop(L, top);
}

/* love.graphics.setColor(darken(self.config.colour, self.states.hover.is and 0.5 or 0.3, true)) */
static void draw_emboss(lua_State *L, const Frame *f)
{
    int top = lua_gettop(L);
    int emboss = push(L, f->config, KEY_EMBOSS);

    if (!lua_toboolean(L, emboss)) {
        lua_settop(L, top);
        return;
    }
    lua_Number percent = is_set(L, f->states, KEY_HOVER) ? 0.5 : 0.3;
    int colour = push(L, f->config, KEY_COLOUR);
    lua_Number dark[3] = {
        element(L, colour, 1) * (1 - percent),
        element(L, colour, 2) * (1 - percent),
        element(L, colour, 3) * (1 - percent)
    };
    lua_pushinteger(L, 4);
    lua_gettable(L, colour);
    begin(L, f, KEY_SET_COLOR);
    for (int i = 0; i < 3; i++)
        lua_pushnumber(L, dark[i]);
    lua_pushvalue(L, -5);
    lua_call(L, 4, 0);
    pixellated_rect(L, f, VALUE_EMBOSS_TYPE, emboss);
    lua_settop(L, top);
}

/* self.ARGS.button_colours[2]: the hover colour while the button is hovered or just clicked. */
static void set_hover_colour(lua_State *L, const Frame *f, int colours)
{
    int top = lua_gettop(L);
    int collided = push(L, f->config, KEY_BUTTON_UIE);
    if (!lua_toboolean(L, collided)) {
        lua_pop(L, 1);
        lua_pushvalue(L, f->self);
    }
    int lit = (truthy(L, push(L, collided, KEY_CONFIG), KEY_HOVER) && is_set(L, push(L, collided, KEY_STATES), KEY_HOVER))
        || clicked_recently(L, f, collided);

    lua_pushinteger(L, 2);
    if (lit) {
        int c = push(L, f->g, KEY_C);
        int ui = push(L, c, KEY_UI);
        get(L, ui, KEY_HOVER_COLOUR);
        lua_remove(L, ui);
        lua_remove(L, c);
    } else {
        lua_pushnil(L);
    }
    lua_settable(L, colours);
    lua_settop(L, top);
}

/* The `self.UIT == G.UIT.B or C or R or ROOT` branch, without a button delay or a progress bar. */
static void draw_box(lua_State *L, const Frame *f, lua_Number colour_alpha_value)
{
    int top = lua_gettop(L);

    prep_draw(L, f, 1);
    graphics_scale(L, f, 1 / (f->tilesize));
    draw_box_shadow(L, f, colour_alpha_value);
    graphics_scale(L, f, f->button_being_pressed ? 0.985 : 1);
    draw_emboss(L, f);

    int args = push(L, f->self, KEY_ARGS);
    int colours = push(L, args, KEY_BUTTON_COLOURS);
    if (!lua_toboolean(L, colours)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, UPVALUE(KEY_BUTTON_COLOURS));
        lua_pushvalue(L, -2);
        lua_settable(L, args);
    }
    lua_pushinteger(L, 1);
    get(L, f->config, KEY_COLOUR);
    lua_settable(L, colours);
    set_hover_colour(L, f, colours);

    for (int i = 1;; i++) {
        lua_rawgeti(L, colours, i);
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            break;
        }
        set_colour_table(L, f, lua_gettop(L));
        lua_pop(L, 1);
        if (rounded(L, f))
            pixellated_rect(L, f, VALUE_FILL, 0);
        else
            plain_rect(L, f, 0, 0);
    }
    graphics_pop(L, f);
    lua_settop(L, top);
}

static int is_uit(lua_State *L, const Frame *f, int uit, enum Key kind)
{
    return fields_equal(L, f->self, KEY_UIT, uit, kind);
}

/* The `if self.config.colour[4] > 0.01` block. */
static void draw_content(lua_State *L, const Frame *f)
{
    lua_Number alpha = colour_alpha(L, f->config, KEY_COLOUR);
    if (!(alpha > 0.01))
        return;

    int top = lua_gettop(L);
    int uit = push(L, f->g, KEY_UIT);
    if (is_uit(L, f, uit, KEY_UIT_T) && truthy(L, f->config, KEY_SCALE)) {
        draw_text(L, f, alpha);
    } else if (is_uit(L, f, uit, KEY_UIT_B) || is_uit(L, f, uit, KEY_UIT_C) || is_uit(L, f, uit, KEY_UIT_R)
        || is_uit(L, f, uit, KEY_UIT_ROOT)) {
        draw_box(L, f, alpha);
    } else if (is_uit(L, f, uit, KEY_UIT_O) && truthy(L, f->config, KEY_OBJECT)) {
        lua_pushvalue(L, UPVALUE(KEY_OBJECT_FOCUS_TIMER));
        lua_pushnil(L);
        lua_settable(L, f->self);
        int object = push(L, f->config, KEY_OBJECT);
        get(L, object, KEY_DRAW);
        lua_pushvalue(L, object);
        lua_call(L, 1, 0);
    }
    lua_settop(L, top);
}

static int wants_drawhash(lua_State *L, const Frame *f)
{
    return truthy(L, f->config, KEY_FORCE_FOCUS) || truthy(L, f->config, KEY_FORCE_COLLISION)
        || truthy(L, f->config, KEY_BUTTON_UIE) || truthy(L, f->config, KEY_BUTTON)
        || nested_truthy(L, f->states, KEY_COLLIDE, KEY_CAN);
}

/* UIElement:draw_self() */
static int draw_self(lua_State *L)
{
    lua_settop(L, 1);
    luaL_checkstack(L, STACK_NEEDED, "UIElement:draw_self");
    Frame f = { 0 };
    f.self = 1;
    f.g = push(L, LUA_GLOBALSINDEX, KEY_G);
    f.states = push(L, f.self, KEY_STATES);
    f.config = push(L, f.self, KEY_CONFIG);

    if (!truthy(L, f.states, KEY_VISIBLE)) {
        if (truthy(L, f.config, KEY_FORCE_FOCUS))
            add_to_drawhash(L, f.g, f.self);
        return 0;
    }
    if (needs_lua(L, &f)) {
        lua_pushvalue(L, ORIGINAL);
        lua_pushvalue(L, f.self);
        lua_call(L, 1, 0);
        return 0;
    }
    f.gfx = push(L, push(L, LUA_GLOBALSINDEX, KEY_LOVE), KEY_GRAPHICS);
    f.tilesize = number(L, f.g, KEY_TILESIZE);
    f.parallax_dist = 1.5;
    f.button_active = 1;

    if (wants_drawhash(L, &f))
        add_to_drawhash(L, f.g, f.self);
    move_button(L, &f);
    draw_content(L, &f);

    lua_pushvalue(L, UPVALUE(KEY_FOCUS_TIMER));
    lua_pushnil(L);
    lua_settable(L, f.self);
    /* Node:draw_boundingrect without G.DEBUG */
    lua_pushvalue(L, UPVALUE(KEY_UNDER_OVERLAY));
    get(L, f.g, KEY_UNDER_OVERLAY);
    lua_settable(L, f.self);
    return 0;
}

/* game_native.ui_draw_self(original): the C draw_self, which calls `original`, the game's
 * UIElement.draw_self, for the rare elements. */
static int make_draw_self(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TFUNCTION);
    luaL_checkstack(L, KEY_COUNT + 2, "ui_draw_self");
    for (int key = 0; key < KEY_COUNT; key++)
        lua_pushstring(L, KEY_NAMES[key]);
    lua_pushvalue(L, 1);
    lua_pushcclosure(L, draw_self, KEY_COUNT + 1);
    return 1;
}

void game_native_register_ui(lua_State *L)
{
    lua_pushcfunction(L, make_draw_self);
    lua_setfield(L, -2, "ui_draw_self");
}
