/*
 * Diagnostic probe for the lua-test crash.
 *
 * Purpose: find out where the failure happens, not to fix it. It never returns
 * from main, so a crash dialog can only mean a genuine fault rather than an exit
 * path, and it records every step twice - to the TTY and to an unbuffered file -
 * so the trail survives whatever kills the process.
 */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include <sys/process.h>
#include <sysutil/sysutil.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

SYS_PROCESS_PARAM(1001, 0x100000)

int luaopen_bit(lua_State *L);

static FILE *log_file;

static void probe_log(const char *format, ...)
{
    va_list args;
    char buffer[512];

    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    printf("probe: %s\n", buffer);
    fflush(stdout);

    if (log_file != NULL) {
        fprintf(log_file, "probe: %s\n", buffer);
        fflush(log_file);
    }
}

static const char *SCRIPT =
    "local out = {}\n"
    "out[#out+1] = 'version ' .. _VERSION\n"
    "out[#out+1] = 'arith ' .. (7*6) .. ' ' .. (2^10) .. ' ' .. (0.1+0.2)\n"
    "out[#out+1] = 'string ' .. ('abc'):upper() .. ' ' .. string.format('%d|%.2f', 42, 3.14159)\n"
    "out[#out+1] = 'table ' .. #({1,2,3}) .. ' ' .. table.concat({'a','b'}, '-')\n"
    "out[#out+1] = 'bit ' .. bit.band(0xF0,0x3C) .. ' ' .. bit.lshift(1,8)\n"
    "out[#out+1] = 'pcall ' .. tostring(pcall(function() error('boom') end))\n"
    "out[#out+1] = 'gc ' .. math.floor(collectgarbage('count')) .. 'kb'\n"
    "return out\n";

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    log_file = fopen("/dev_hdd0/tmp/probe.log", "w");
    if (log_file != NULL)
        setvbuf(log_file, NULL, _IONBF, 0);

    probe_log("start (log file %s)", log_file != NULL ? "open" : "UNAVAILABLE");

    lua_State *L = luaL_newstate();
    probe_log("newstate %s", L != NULL ? "ok" : "FAILED");
    if (L == NULL)
        goto spin;

    luaL_openlibs(L);
    probe_log("openlibs ok");

    lua_pushcfunction(L, luaopen_bit);
    lua_call(L, 0, 0);
    probe_log("bit registered");

    int status = luaL_loadstring(L, SCRIPT);
    probe_log("loadstring status %d", status);
    if (status != 0) {
        probe_log("load error: %s", lua_tostring(L, -1));
        goto spin;
    }

    status = lua_pcall(L, 0, 1, 0);
    probe_log("pcall status %d", status);
    if (status != 0) {
        probe_log("run error: %s", lua_tostring(L, -1));
        goto spin;
    }

    int results = (int)lua_objlen(L, -1);
    probe_log("results %d", results);

    for (int index = 1; index <= results; index++) {
        lua_rawgeti(L, -1, index);
        probe_log("out %s", lua_tostring(L, -1));
        lua_pop(L, 1);
    }

    lua_close(L);
    probe_log("closed - everything ran");

spin:
    probe_log("entering loop; close the emulator to stop");
    while (1) {
        sysUtilCheckCallback();
        sleep(1);
    }

    return 0;
}
