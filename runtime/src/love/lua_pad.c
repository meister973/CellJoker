/* love_platform.read_pad: one pad's state, for runtime/lua/love_joystick.lua. */
#include <stdbool.h>
#include <string.h>

#include <lua.h>
#include <lauxlib.h>

#include <io/pad.h>

#include "lua_platform.h"

#define PORT_COUNT 7

#define STICK_CENTER 128

static padData last_data[PORT_COUNT];

/* Sticks rest at 128, so zeroed data would read as both sticks pushed fully up and left. */
static void reset_data(int port)
{
    memset(&last_data[port], 0, sizeof(padData));
    last_data[port].ANA_L_H = STICK_CENTER;
    last_data[port].ANA_L_V = STICK_CENTER;
    last_data[port].ANA_R_H = STICK_CENTER;
    last_data[port].ANA_R_V = STICK_CENTER;
}

/* ioPadGetData reports len 0 when nothing changed since the last call, so the last real
 * state has to be kept, or a held button would look released. */
static const padData *current_data(int port, bool connected)
{
    padData fresh;

    if (!connected) {
        reset_data(port);
        return &last_data[port];
    }

    if (ioPadGetData(port, &fresh) == 0 && fresh.len > 0)
        last_data[port] = fresh;
    return &last_data[port];
}

static void clear_table(lua_State *L, int table)
{
    lua_pushnil(L);
    while (lua_next(L, table) != 0) {
        lua_pop(L, 1);
        lua_pushvalue(L, -1);
        lua_pushnil(L);
        lua_rawset(L, table);
    }
}

static void set_if_held(lua_State *L, int table, const char *name, int held)
{
    if (held) {
        lua_pushboolean(L, 1);
        lua_setfield(L, table, name);
    }
}

/* Names are LÖVE's, with cross as "a" as SDL maps a PlayStation pad. */
static void fill_buttons(lua_State *L, int table, const padData *data)
{
    clear_table(L, table);
    set_if_held(L, table, "a", data->BTN_CROSS);
    set_if_held(L, table, "b", data->BTN_CIRCLE);
    set_if_held(L, table, "x", data->BTN_SQUARE);
    set_if_held(L, table, "y", data->BTN_TRIANGLE);
    set_if_held(L, table, "dpup", data->BTN_UP);
    set_if_held(L, table, "dpdown", data->BTN_DOWN);
    set_if_held(L, table, "dpleft", data->BTN_LEFT);
    set_if_held(L, table, "dpright", data->BTN_RIGHT);
    set_if_held(L, table, "start", data->BTN_START);
    set_if_held(L, table, "back", data->BTN_SELECT);
    set_if_held(L, table, "leftshoulder", data->BTN_L1);
    set_if_held(L, table, "rightshoulder", data->BTN_R1);
    set_if_held(L, table, "leftstick", data->BTN_L3);
    set_if_held(L, table, "rightstick", data->BTN_R3);
    set_if_held(L, table, "triggerleft", data->BTN_L2);
    set_if_held(L, table, "triggerright", data->BTN_R2);
}

/* read_pad(port, buttons): returns connected, then the sticks as lx, ly, rx, ry. */
static int platform_read_pad(lua_State *L)
{
    padInfo info;
    int port = luaL_checkint(L, 1);

    luaL_checktype(L, 2, LUA_TTABLE);
    luaL_argcheck(L, port >= 0 && port < PORT_COUNT, 1, "port out of range");

    /* Checked each frame: a pad can sleep and come back on another port. */
    bool connected = ioPadGetInfo(&info) == 0 && info.status[port] != 0;
    const padData *data = current_data(port, connected);

    fill_buttons(L, 2, data);
    lua_pushboolean(L, connected);
    lua_pushnumber(L, data->ANA_L_H);
    lua_pushnumber(L, data->ANA_L_V);
    lua_pushnumber(L, data->ANA_R_H);
    lua_pushnumber(L, data->ANA_R_V);
    return 5;
}

int lua_pad_register(lua_State *L)
{
    if (ioPadInit(PORT_COUNT) != 0)
        return luaL_error(L, "love.joystick: ioPadInit failed");

    for (int port = 0; port < PORT_COUNT; port++)
        reset_data(port);

    lua_pushcfunction(L, platform_read_pad);
    lua_setfield(L, -2, "read_pad");
    return 0;
}
