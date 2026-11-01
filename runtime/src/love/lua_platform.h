#ifndef LUA_PLATFORM_H
#define LUA_PLATFORM_H

#include <stdbool.h>

#include <lua.h>

int luaopen_love_platform(lua_State *L);
int luaopen_love_data(lua_State *L);
int luaopen_love_audio(lua_State *L);

/* Services system callbacks, and says whether the user chose Quit Game in the XMB. */
bool love_platform_poll_exit(void);

/* Adds read_pad to the love_platform table on top of the stack, and starts the pad service. */
int lua_pad_register(lua_State *L);

#endif
