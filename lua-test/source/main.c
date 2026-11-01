/*
 * Phase 2 gate: a real Lua 5.1 interpreter running on the Cell.
 *
 * Runs a script that exercises the parts of the language and the standard
 * library the game actually leans on, plus the `bit` module it requires at
 * startup. Reports byte order at runtime, because every asset loader downstream
 * depends on that being big-endian.
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/process.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

SYS_PROCESS_PARAM(1001, 0x100000)

int luaopen_bit(lua_State *L);

static const char *SCRIPT =
    "local out = {}\n"
    "out[#out+1] = 'version ' .. _VERSION\n"
    "out[#out+1] = 'arith ' .. (7*6) .. ' ' .. (2^10) .. ' ' .. (0.1+0.2)\n"
    "out[#out+1] = 'string ' .. ('abc'):upper() .. ' ' .. string.format('%d|%.2f', 42, 3.14159)\n"
    "out[#out+1] = 'table ' .. #({1,2,3}) .. ' ' .. table.concat({'a','b'}, '-')\n"
    "out[#out+1] = 'bit ' .. bit.band(0xF0,0x3C) .. ' ' .. bit.lshift(1,8) .. ' 0x' .. string.format('%X', bit.bxor(0xAAAA,0x5555))\n"
    "out[#out+1] = 'pcall ' .. tostring(pcall(function() error('boom') end))\n"
    "out[#out+1] = 'gc ' .. math.floor(collectgarbage('count')) .. 'kb'\n"
    "return out\n";

int main(int argc, char **argv)
{
    union {
        unsigned int value;
        unsigned char bytes[4];
    } probe;

    probe.value = 0x01020304u;
    printf("lua-test: alive\n");
    printf("lua-test: endian %s\n", probe.bytes[0] == 1 ? "big" : "little");
    fflush(stdout);

    lua_State *L = luaL_newstate();
    if (L == NULL) {
        printf("lua-test: FAIL could not create state\n");
        return 1;
    }

    luaL_openlibs(L);

    lua_pushcfunction(L, luaopen_bit);
    lua_call(L, 0, 0);

    int status = luaL_loadstring(L, SCRIPT);
    if (status == 0)
        status = lua_pcall(L, 0, 1, 0);

    if (status != 0) {
        printf("lua-test: FAIL %s\n", lua_tostring(L, -1));
        fflush(stdout);
        lua_close(L);
        return 1;
    }

    int results = (int)lua_objlen(L, -1);
    for (int index = 1; index <= results; index++) {
        lua_rawgeti(L, -1, index);
        printf("lua-test: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
    }

    lua_close(L);
    printf("lua-test: done\n");
    fflush(stdout);

    sleep(2);
    return 0;
}
