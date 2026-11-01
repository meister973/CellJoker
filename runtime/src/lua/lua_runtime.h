#ifndef LUA_RUNTIME_H
#define LUA_RUNTIME_H

#include <lua.h>

/* Everything the game needs on top of a stock Lua 5.1 state: bit, then LuaJIT behaviour. */
void lua_runtime_open(lua_State *L);

int luaopen_bit(lua_State *L);
int luaopen_luajit_compat(lua_State *L);

#endif
