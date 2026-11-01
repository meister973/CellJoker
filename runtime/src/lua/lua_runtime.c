#include "lua_runtime.h"

static void call_opener(lua_State *L, lua_CFunction opener)
{
    lua_pushcfunction(L, opener);
    lua_call(L, 0, 0);
}

void lua_runtime_open(lua_State *L)
{
    call_opener(L, luaopen_bit);
    call_opener(L, luaopen_luajit_compat);
}
