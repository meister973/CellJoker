/*
 * Moveable:move (engine/moveable.lua) in C, with the same reads, the same arithmetic in the same
 * order and the same writes as the Lua version. The rare paths (a changed alignment, glue,
 * lr_clamp, juice, get_major) call the game's own Lua methods. Only the per-frame easing runs
 * here. The host runner's HARNESS_STATE_DUMP compares whole runs with the Lua version.
 */
#include <math.h>

#include <lua.h>
#include <lauxlib.h>

#include "lua_runtime.h"

/* Field names and string values, made once and kept as upvalues: lua_getfield would hash and
 * intern its key string on every call. */
enum Key {
    KEY_FRAME, KEY_MOVE, KEY_FRAME_MAJOR, KEY_OLD_MAJOR, KEY_G, KEY_FRAMES, KEY_CREATED_ON_PAUSE,
    KEY_SETTINGS, KEY_PAUSED, KEY_ALIGNMENT, KEY_TYPE, KEY_PREV_TYPE, KEY_OFFSET, KEY_PREV_OFFSET,
    KEY_ALIGN_TO_MAJOR, KEY_CALCING, KEY_ROLE, KEY_ROLE_TYPE, KEY_MAJOR, KEY_GLUE_TO_MAJOR,
    KEY_MOVE_METHOD, KEY_STATIONARY, KEY_NEW_ALIGNMENT, KEY_CONFIG, KEY_REFRESH_MOVEMENT,
    KEY_JUICE, KEY_MOVE_JUICE, KEY_XY_BOND, KEY_R_BOND, KEY_SCALE_BOND, KEY_WH_BOND, KEY_LR_CLAMP,
    KEY_GET_MAJOR, KEY_T, KEY_VT, KEY_VELOCITY, KEY_X, KEY_Y, KEY_W, KEY_H, KEY_R, KEY_SCALE,
    KEY_EXP_TIMES, KEY_XY, KEY_MAX_VEL, KEY_ZOOM, KEY_STATES, KEY_DRAG, KEY_HOVER, KEY_IS,
    KEY_PINCH, KEY_ROOM, KEY_SHADOW_PARRALLAX,
    VALUE_GLUED, VALUE_MINOR, VALUE_MAJOR, VALUE_WEAK, VALUE_STRONG,
    KEY_COUNT
};

static const char *const KEY_NAMES[KEY_COUNT] = {
    "FRAME", "MOVE", "MAJOR", "OLD_MAJOR", "G", "FRAMES", "created_on_pause",
    "SETTINGS", "paused", "alignment", "type", "prev_type", "offset", "prev_offset",
    "align_to_major", "CALCING", "role", "role_type", "major", "glue_to_major",
    "move", "STATIONARY", "NEW_ALIGNMENT", "config", "refresh_movement",
    "juice", "move_juice", "xy_bond", "r_bond", "scale_bond", "wh_bond", "lr_clamp",
    "get_major", "T", "VT", "velocity", "x", "y", "w", "h", "r", "scale",
    "exp_times", "xy", "max_vel", "zoom", "states", "drag", "hover", "is",
    "pinch", "ROOM", "shadow_parrallax",
    "Glued", "Minor", "Major", "Weak", "Strong"
};

/* The most stack slots that move uses at once, nested calls not counted. */
enum { STACK_NEEDED = 40 };

#define UPVALUE(key) lua_upvalueindex((key) + 1)

/* Pushes table[key], with metamethods, as `table.key` does in Lua. `table` is an absolute index. */
static void get(lua_State *L, int table, enum Key key)
{
    lua_pushvalue(L, UPVALUE(key));
    lua_gettable(L, table);
}

/* Pushes table[key] and returns its absolute index. */
static int push(lua_State *L, int table, enum Key key)
{
    get(L, table, key);
    return lua_gettop(L);
}

static lua_Number number(lua_State *L, int table, enum Key key)
{
    get(L, table, key);
    if (!lua_isnumber(L, -1))
        luaL_error(L, "Moveable:move: %s is %s, not a number", KEY_NAMES[key], luaL_typename(L, -1));
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

/* `table.key == 'value'` for one of the VALUE_ strings. */
static int equals(lua_State *L, int table, enum Key key, enum Key value)
{
    get(L, table, key);
    int same = lua_rawequal(L, -1, UPVALUE(value));
    lua_pop(L, 1);
    return same;
}

static void set_number(lua_State *L, int table, enum Key key, lua_Number value)
{
    lua_pushvalue(L, UPVALUE(key));
    lua_pushnumber(L, value);
    lua_settable(L, table);
}

static void set_boolean(lua_State *L, int table, enum Key key, int value)
{
    lua_pushvalue(L, UPVALUE(key));
    lua_pushboolean(L, value);
    lua_settable(L, table);
}

static void set_nil(lua_State *L, int table, enum Key key)
{
    lua_pushvalue(L, UPVALUE(key));
    lua_pushnil(L);
    lua_settable(L, table);
}

/* `object:method(dt)`, or `object:method()` when dt is 0. */
static void call_method(lua_State *L, int object, enum Key method, int dt)
{
    get(L, object, method);
    lua_pushvalue(L, object);
    if (!dt) {
        lua_call(L, 1, 0);
        return;
    }
    lua_pushvalue(L, dt);
    lua_call(L, 2, 0);
}

/* dt as a number, read only where the Lua version does arithmetic with it. */
static lua_Number dt_number(lua_State *L, int dt)
{
    if (!lua_isnumber(L, dt))
        luaL_error(L, "Moveable:move: dt is %s, not a number", luaL_typename(L, dt));
    return lua_tonumber(L, dt);
}

/* `self.juice and self.juice.key or 0` */
static lua_Number juice_or_zero(lua_State *L, int self, enum Key key)
{
    int top = lua_gettop(L);
    int juice = push(L, self, KEY_JUICE);
    lua_Number value = 0;

    if (lua_toboolean(L, juice)) {
        get(L, juice, key);
        if (lua_toboolean(L, -1))
            value = number(L, juice, key);
    }
    lua_settop(L, top);
    return value;
}

/* Lua 5.1's math.min(a, b) and math.max(a, b), with their NaN behaviour. */
static lua_Number lua_min(lua_Number a, lua_Number b)
{
    return b < a ? b : a;
}

static lua_Number lua_max(lua_Number a, lua_Number b)
{
    return b > a ? b : a;
}

/* Moveable:move_juice runs only when self.juice is set, and does nothing without it. */
static void move_juice(lua_State *L, int self, int dt)
{
    if (truthy(L, self, KEY_JUICE))
        call_method(L, self, KEY_MOVE_JUICE, dt);
}

/* Moveable:move_xy */
static void move_xy(lua_State *L, int self, int dt, int g)
{
    int top = lua_gettop(L);
    int t = push(L, self, KEY_T);
    int vt = push(L, self, KEY_VT);
    int velocity = push(L, self, KEY_VELOCITY);
    lua_Number tx = number(L, t, KEY_X), vtx = number(L, vt, KEY_X), vx = number(L, velocity, KEY_X);
    lua_Number ty = number(L, t, KEY_Y), vty = number(L, vt, KEY_Y), vy = number(L, velocity, KEY_Y);

    if (!(tx != vtx || fabs(vx) > 0.01 || ty != vty || fabs(vy) > 0.01)) {
        lua_settop(L, top);
        return;
    }
    int exp_times = push(L, g, KEY_EXP_TIMES);
    lua_Number ease = number(L, exp_times, KEY_XY);
    lua_Number step = dt_number(L, dt);
    vx = ease * vx + (1 - ease) * (tx - vtx) * 35 * step;
    vy = ease * vy + (1 - ease) * (ty - vty) * 35 * step;
    set_number(L, velocity, KEY_X, vx);
    set_number(L, velocity, KEY_Y, vy);

    lua_Number max_vel = number(L, exp_times, KEY_MAX_VEL);
    if (vx * vx + vy * vy > max_vel * max_vel) {
        lua_Number actual_vel = sqrt(vx * vx + vy * vy);
        vx = max_vel * vx / actual_vel;
        vy = max_vel * vy / actual_vel;
        set_number(L, velocity, KEY_X, vx);
        set_number(L, velocity, KEY_Y, vy);
    }
    set_boolean(L, self, KEY_STATIONARY, 0);
    vtx = vtx + vx;
    vty = vty + vy;
    set_number(L, vt, KEY_X, vtx);
    set_number(L, vt, KEY_Y, vty);
    if (fabs(vtx - tx) < 0.01 && fabs(vx) < 0.01) {
        set_number(L, vt, KEY_X, tx);
        set_number(L, velocity, KEY_X, 0);
    }
    if (fabs(vty - ty) < 0.01 && fabs(vy) < 0.01) {
        set_number(L, vt, KEY_Y, ty);
        set_number(L, velocity, KEY_Y, 0);
    }
    lua_settop(L, top);
}

/* `self.juice and self.juice.r*2 or 0`: a product is never false, so only juice is tested. */
static lua_Number juice_r_twice(lua_State *L, int self)
{
    int top = lua_gettop(L);
    int juice = push(L, self, KEY_JUICE);
    lua_Number value = 0;

    if (lua_toboolean(L, juice))
        value = number(L, juice, KEY_R) * 2;
    lua_settop(L, top);
    return value;
}

/* Moveable:move_r(dt, self.velocity) */
static void move_r(lua_State *L, int self, int dt, int g)
{
    int top = lua_gettop(L);
    int t = push(L, self, KEY_T);
    int vt = push(L, self, KEY_VT);
    int velocity = push(L, self, KEY_VELOCITY);
    lua_Number tr = number(L, t, KEY_R);
    lua_Number des_r = tr + 0.015 * number(L, velocity, KEY_X) / dt_number(L, dt) + juice_r_twice(L, self);
    lua_Number vtr = number(L, vt, KEY_R);
    lua_Number vr = number(L, velocity, KEY_R);

    if (des_r != vtr || fabs(vr) > 0.001) {
        set_boolean(L, self, KEY_STATIONARY, 0);
        int exp_times = push(L, g, KEY_EXP_TIMES);
        lua_Number ease = number(L, exp_times, KEY_R);
        vr = ease * vr + (1 - ease) * (des_r - vtr);
        set_number(L, velocity, KEY_R, vr);
        vtr = vtr + vr;
        set_number(L, vt, KEY_R, vtr);
    }
    if (fabs(vtr - tr) < 0.001 && fabs(vr) < 0.001) {
        set_number(L, vt, KEY_R, tr);
        set_number(L, velocity, KEY_R, 0);
    }
    lua_settop(L, top);
}

/* `self.zoom and ((self.states.drag.is and 0.1 or 0) + (self.states.hover.is and 0.05 or 0)) or 0` */
static lua_Number zoom_scale(lua_State *L, int self)
{
    if (!truthy(L, self, KEY_ZOOM))
        return 0;

    int top = lua_gettop(L);
    int states = push(L, self, KEY_STATES);
    int drag = push(L, states, KEY_DRAG);
    lua_Number dragged = truthy(L, drag, KEY_IS) ? 0.1 : 0;
    int hover = push(L, states, KEY_HOVER);
    lua_Number hovered = truthy(L, hover, KEY_IS) ? 0.05 : 0;
    lua_settop(L, top);
    return dragged + hovered;
}

/* Moveable:move_scale */
static void move_scale(lua_State *L, int self, int g)
{
    int top = lua_gettop(L);
    int t = push(L, self, KEY_T);
    int vt = push(L, self, KEY_VT);
    int velocity = push(L, self, KEY_VELOCITY);
    lua_Number des_scale = number(L, t, KEY_SCALE) + zoom_scale(L, self) + juice_or_zero(L, self, KEY_SCALE);
    lua_Number vt_scale = number(L, vt, KEY_SCALE);
    lua_Number v_scale = number(L, velocity, KEY_SCALE);

    if (des_scale != vt_scale || fabs(v_scale) > 0.001) {
        set_boolean(L, self, KEY_STATIONARY, 0);
        int exp_times = push(L, g, KEY_EXP_TIMES);
        lua_Number ease = number(L, exp_times, KEY_SCALE);
        v_scale = ease * v_scale + (1 - ease) * (des_scale - vt_scale);
        set_number(L, velocity, KEY_SCALE, v_scale);
        set_number(L, vt, KEY_SCALE, vt_scale + v_scale);
    }
    lua_settop(L, top);
}

/* Moveable:move_wh */
static void move_wh(lua_State *L, int self, int dt)
{
    int top = lua_gettop(L);
    int t = push(L, self, KEY_T);
    int vt = push(L, self, KEY_VT);
    int pinch = push(L, self, KEY_PINCH);
    int pinch_x = truthy(L, pinch, KEY_X), pinch_y = truthy(L, pinch, KEY_Y);
    lua_Number tw = number(L, t, KEY_W), vtw = number(L, vt, KEY_W);
    lua_Number th = number(L, t, KEY_H), vth = number(L, vt, KEY_H);

    if ((tw != vtw && !pinch_x) || (th != vth && !pinch_y) || (vtw > 0 && pinch_x) || (vth > 0 && pinch_y)) {
        set_boolean(L, self, KEY_STATIONARY, 0);
        lua_Number step = dt_number(L, dt);
        vtw = vtw + (8 * step) * (pinch_x ? -1 : 1) * tw;
        vth = vth + (8 * step) * (pinch_y ? -1 : 1) * th;
        set_number(L, vt, KEY_W, lua_max(lua_min(vtw, tw), 0));
        set_number(L, vt, KEY_H, lua_max(lua_min(vth, th), 0));
    }
    lua_settop(L, top);
}

/* Moveable:calculate_parrallax */
static void calculate_parrallax(lua_State *L, int self, int g)
{
    int top = lua_gettop(L);
    int room = push(L, g, KEY_ROOM);

    if (!lua_toboolean(L, room)) {
        lua_settop(L, top);
        return;
    }
    int t = push(L, self, KEY_T);
    int room_t = push(L, room, KEY_T);
    lua_Number room_w = number(L, room_t, KEY_W);
    lua_Number x = (number(L, t, KEY_X) + number(L, t, KEY_W) / 2 - room_w / 2) / (room_w / 2) * 1.5;
    int shadow = push(L, self, KEY_SHADOW_PARRALLAX);
    set_number(L, shadow, KEY_X, x);
    lua_settop(L, top);
}

/* MWM.rotated_offset in Moveable:move_with_major. */
typedef struct {
    lua_Number x, y;
} Offset;

static Offset rotated_offset(lua_State *L, int self, int role, int major_tab, int major)
{
    int top = lua_gettop(L);
    int role_offset = push(L, role, KEY_OFFSET);
    int tab_offset = push(L, major_tab, KEY_OFFSET);
    Offset sum = {
        number(L, role_offset, KEY_X) + number(L, tab_offset, KEY_X),
        number(L, role_offset, KEY_Y) + number(L, tab_offset, KEY_Y)
    };

    if (equals(L, role, KEY_R_BOND, VALUE_WEAK)) {
        lua_settop(L, top);
        return sum;
    }
    int major_vt = push(L, major, KEY_VT);
    lua_Number angle = number(L, major_vt, KEY_R);
    if (angle < 0.0001 && angle > -0.0001) {
        lua_settop(L, top);
        return sum;
    }
    int t = push(L, self, KEY_T);
    int major_t = push(L, major, KEY_T);
    lua_Number cosine = cos(angle), sine = sin(angle);
    lua_Number half_w = -number(L, t, KEY_W) / 2 + number(L, major_t, KEY_W) / 2;
    lua_Number half_h = -number(L, t, KEY_H) / 2 + number(L, major_t, KEY_H) / 2;
    lua_Number offs_x = number(L, role_offset, KEY_X) + number(L, tab_offset, KEY_X) - half_w;
    lua_Number offs_y = number(L, role_offset, KEY_Y) + number(L, tab_offset, KEY_Y) - half_h;
    Offset rotated = {
        offs_x * cosine - offs_y * sine + half_w,
        offs_x * sine + offs_y * cosine + half_h
    };
    lua_settop(L, top);
    return rotated;
}

/* The *_bond == 'Strong' parts of Moveable:move_with_major, each with its 'Weak' fallback. */
static void follow_major(lua_State *L, int self, int dt, int g, int role, int major, Offset offset)
{
    int t = push(L, self, KEY_T);
    int vt = push(L, self, KEY_VT);
    int major_t = push(L, major, KEY_T);
    int major_vt = push(L, major, KEY_VT);

    if (equals(L, role, KEY_XY_BOND, VALUE_STRONG)) {
        set_number(L, vt, KEY_X, number(L, major_vt, KEY_X) + offset.x);
        set_number(L, vt, KEY_Y, number(L, major_vt, KEY_Y) + offset.y);
    } else if (equals(L, role, KEY_XY_BOND, VALUE_WEAK)) {
        move_xy(L, self, dt, g);
    }

    if (equals(L, role, KEY_R_BOND, VALUE_STRONG)) {
        lua_Number r = number(L, t, KEY_R) + number(L, major_vt, KEY_R);
        set_number(L, vt, KEY_R, r + juice_or_zero(L, self, KEY_R));
    } else if (equals(L, role, KEY_R_BOND, VALUE_WEAK)) {
        move_r(L, self, dt, g);
    }

    if (equals(L, role, KEY_SCALE_BOND, VALUE_STRONG)) {
        lua_Number scale = number(L, t, KEY_SCALE) * (number(L, major_vt, KEY_SCALE) / number(L, major_t, KEY_SCALE));
        set_number(L, vt, KEY_SCALE, scale + juice_or_zero(L, self, KEY_SCALE));
    } else if (equals(L, role, KEY_SCALE_BOND, VALUE_WEAK)) {
        move_scale(L, self, g);
    }

    if (equals(L, role, KEY_WH_BOND, VALUE_STRONG)) {
        lua_Number tw = number(L, t, KEY_W);
        lua_Number vtx = number(L, vt, KEY_X) + (0.5 * (1 - number(L, major_vt, KEY_W) / (number(L, major_t, KEY_W)))) * tw;
        set_number(L, vt, KEY_X, vtx);
        set_number(L, vt, KEY_W, (tw) * (number(L, major_vt, KEY_W) / number(L, major_t, KEY_W)));
        set_number(L, vt, KEY_H, (number(L, t, KEY_H)) * (number(L, major_vt, KEY_H) / number(L, major_t, KEY_H)));
    } else if (equals(L, role, KEY_WH_BOND, VALUE_WEAK)) {
        move_wh(L, self, dt);
    }
}

/* Moveable:move_with_major. The Lua version keeps its offsets in the global MWM, which nothing
 * else reads; this one keeps them in locals. */
static void move_with_major(lua_State *L, int self, int dt, int g)
{
    int top = lua_gettop(L);
    int role = push(L, self, KEY_ROLE);

    if (!equals(L, role, KEY_ROLE_TYPE, VALUE_MINOR)) {
        lua_settop(L, top);
        return;
    }
    /* local major_tab = self.role.major:get_major() */
    int role_major = push(L, role, KEY_MAJOR);
    get(L, role_major, KEY_GET_MAJOR);
    lua_pushvalue(L, role_major);
    lua_call(L, 1, 1);
    int major_tab = lua_gettop(L);

    move_juice(L, self, dt);

    int major = push(L, major_tab, KEY_MAJOR);
    Offset offset = rotated_offset(L, self, role, major_tab, major);
    int t = push(L, self, KEY_T);
    int major_t = push(L, major, KEY_T);
    set_number(L, t, KEY_X, number(L, major_t, KEY_X) + offset.x);
    set_number(L, t, KEY_Y, number(L, major_t, KEY_Y) + offset.y);

    follow_major(L, self, dt, g, role, major, offset);
    calculate_parrallax(L, self, g);
    lua_settop(L, top);
}

/* True when Moveable:align_to_major would return at its first check without changing anything:
 * the type and the offset are the ones of the last alignment. */
static int alignment_unchanged(lua_State *L, int self)
{
    int top = lua_gettop(L);
    int alignment = push(L, self, KEY_ALIGNMENT);

    get(L, alignment, KEY_TYPE);
    get(L, alignment, KEY_PREV_TYPE);
    int same = lua_equal(L, -2, -1);
    if (same) {
        int offset = push(L, alignment, KEY_OFFSET);
        int prev_offset = push(L, alignment, KEY_PREV_OFFSET);
        get(L, prev_offset, KEY_X);
        get(L, offset, KEY_X);
        get(L, prev_offset, KEY_Y);
        get(L, offset, KEY_Y);
        same = lua_equal(L, -4, -3) && lua_equal(L, -2, -1);
    }
    lua_settop(L, top);
    return same;
}

/* The `role_type == 'Minor'` branch of Moveable:move. */
static void move_minor(lua_State *L, int self, int dt, int g, int frames)
{
    int top = lua_gettop(L);
    int role = push(L, self, KEY_ROLE);
    int major = push(L, role, KEY_MAJOR);
    int major_frame = push(L, major, KEY_FRAME);

    if (number(L, major_frame, KEY_MOVE) < number(L, frames, KEY_MOVE))
        call_method(L, major, KEY_MOVE_METHOD, dt);

    lua_settop(L, top);
    role = push(L, self, KEY_ROLE);
    major = push(L, role, KEY_MAJOR);
    lua_pushvalue(L, UPVALUE(KEY_STATIONARY));
    get(L, major, KEY_STATIONARY);
    int stationary = lua_toboolean(L, -1);
    lua_settable(L, self);

    if (!stationary || truthy(L, self, KEY_NEW_ALIGNMENT)
        || truthy(L, push(L, self, KEY_CONFIG), KEY_REFRESH_MOVEMENT)
        || truthy(L, self, KEY_JUICE)
        || equals(L, role, KEY_XY_BOND, VALUE_WEAK)
        || equals(L, role, KEY_R_BOND, VALUE_WEAK)) {
        set_boolean(L, self, KEY_CALCING, 1);
        move_with_major(L, self, dt, g);
    }
    lua_settop(L, top);
}

/* The `role_type == 'Major'` branch of Moveable:move. */
static void move_major(lua_State *L, int self, int dt, int g)
{
    set_boolean(L, self, KEY_STATIONARY, 1);
    move_juice(L, self, dt);
    move_xy(L, self, dt, g);
    move_r(L, self, dt, g);
    move_scale(L, self, g);
    move_wh(L, self, dt);
    calculate_parrallax(L, self, g);
}

static void move_by_role(lua_State *L, int self, int dt, int g, int frames)
{
    int top = lua_gettop(L);
    int role = push(L, self, KEY_ROLE);

    if (equals(L, role, KEY_ROLE_TYPE, VALUE_GLUED)) {
        int major = push(L, role, KEY_MAJOR);
        if (lua_toboolean(L, major)) {
            /* self:glue_to_major(self.role.major) */
            get(L, self, KEY_GLUE_TO_MAJOR);
            lua_pushvalue(L, self);
            lua_pushvalue(L, major);
            lua_call(L, 2, 0);
        }
        lua_settop(L, top);
        return;
    }
    if (equals(L, role, KEY_ROLE_TYPE, VALUE_MINOR) && truthy(L, role, KEY_MAJOR)) {
        lua_settop(L, top);
        move_minor(L, self, dt, g, frames);
        return;
    }
    if (equals(L, role, KEY_ROLE_TYPE, VALUE_MAJOR))
        move_major(L, self, dt, g);
    lua_settop(L, top);
}

/* Moveable:move(dt) */
static int move(lua_State *L)
{
    enum { SELF = 1, DT };

    lua_settop(L, DT);
    luaL_checkstack(L, STACK_NEEDED, "Moveable:move");
    int g = push(L, LUA_GLOBALSINDEX, KEY_G);
    int frames = push(L, g, KEY_FRAMES);
    int frame = push(L, SELF, KEY_FRAME);

    /* `self.FRAME.MOVE >= G.FRAMES.MOVE` is `G.FRAMES.MOVE <= self.FRAME.MOVE` in Lua. */
    if (number(L, frames, KEY_MOVE) <= number(L, frame, KEY_MOVE))
        return 0;
    lua_pushvalue(L, UPVALUE(KEY_OLD_MAJOR));
    get(L, frame, KEY_FRAME_MAJOR);
    lua_settable(L, frame);
    set_nil(L, frame, KEY_FRAME_MAJOR);
    lua_pushvalue(L, UPVALUE(KEY_MOVE));
    get(L, frames, KEY_MOVE);
    lua_settable(L, frame);

    if (!truthy(L, SELF, KEY_CREATED_ON_PAUSE) && truthy(L, push(L, g, KEY_SETTINGS), KEY_PAUSED))
        return 0;

    if (!alignment_unchanged(L, SELF))
        call_method(L, SELF, KEY_ALIGN_TO_MAJOR, 0);

    set_nil(L, SELF, KEY_CALCING);
    move_by_role(L, SELF, DT, g, frames);

    int alignment = push(L, SELF, KEY_ALIGNMENT);
    if (lua_toboolean(L, alignment) && truthy(L, alignment, KEY_LR_CLAMP))
        call_method(L, SELF, KEY_LR_CLAMP, 0);

    set_boolean(L, SELF, KEY_NEW_ALIGNMENT, 0);
    return 0;
}

void game_native_register_move(lua_State *L)
{
    luaL_checkstack(L, KEY_COUNT + 2, "game_native_register_move");
    for (int key = 0; key < KEY_COUNT; key++)
        lua_pushstring(L, KEY_NAMES[key]);
    lua_pushcclosure(L, move, KEY_COUNT);
    lua_setfield(L, -2, "move");
}
