#ifndef LUA_RUNTIME_H
#define LUA_RUNTIME_H

#include <lua.h>

/* Everything the game needs on top of a stock Lua 5.1 state: bit, LuaJIT behaviour, and the
 * engine functions in C. */
void lua_runtime_open(lua_State *L);

int luaopen_bit(lua_State *L);
int luaopen_luajit_compat(lua_State *L);
int luaopen_game_native(lua_State *L);
void game_native_register_move(lua_State *L);
void game_native_register_ui(lua_State *L);
void game_native_register_text(lua_State *L);

#endif
