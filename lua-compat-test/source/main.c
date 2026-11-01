/*
 * Differential test driver: runs tests/lua_compat/cases.lua on the Cell and prints
 * each result to the TTY, for comparison with the same script under real LuaJIT.
 */
#include <stdio.h>

#include <sys/process.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "lua_runtime.h"
#include "cases_lua.h"

SYS_PROCESS_PARAM(1001, 0x100000)

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    lua_State *L = luaL_newstate();
    if (L == NULL) {
        printf("compat: FAIL no state\n");
        return 1;
    }

    luaL_openlibs(L);
    lua_runtime_open(L);

    if (luaL_loadstring(L, CASES_LUA) != 0 || lua_pcall(L, 0, 1, 0) != 0) {
        printf("compat: FAIL %s\n", lua_tostring(L, -1));
        fflush(stdout);
        return 1;
    }

    int count = (int)lua_objlen(L, -1);
    for (int index = 1; index <= count; index++) {
        lua_rawgeti(L, -1, index);
        printf("case: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
    }

    printf("compat: done %d\n", count);
    fflush(stdout);
    return 0;
}
