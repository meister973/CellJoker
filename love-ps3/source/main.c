/*
 * love-ps3 entry point: a Lua state, the native modules, then boot.lua from the
 * install folder. The game owns the process until the user quits from the XMB.
 */
#include <stdio.h>

#include <sys/process.h>
#include <unistd.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "audio_mixer.h"
#include "gfx.h"
#include "lua_graphics.h"
#include "lua_platform.h"
#include "lua_runtime.h"

/* The Lua parser and the game's nested UI code recurse deeply on the main thread. */
SYS_PROCESS_PARAM(1001, 0x200000)

#define INSTALL_ROOT "/dev_hdd0/game/BLTR00001/USRDIR/"
#define LOG_PATH "/dev_hdd0/tmp/balatro_log.txt"

static int traceback(lua_State *L)
{
    lua_getglobal(L, "debug");
    lua_getfield(L, -1, "traceback");
    lua_pushvalue(L, 1);
    lua_pushinteger(L, 2);
    lua_call(L, 2, 1);
    return 1;
}

/* Kept simple so that it still works when nothing else has started. */
static void write_log(const char *mode, const char *message)
{
    FILE *file = fopen(LOG_PATH, mode);

    if (file == NULL)
        return;
    fprintf(file, "%s\n", message);
    fclose(file);
}

/* After a failure the last frame stays on screen until the user quits from the XMB. */
static void wait_for_quit(void)
{
    fflush(stdout);
    while (!love_platform_poll_exit())
        sleep(1);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("love-ps3: start\n");
    fflush(stdout);
    write_log("w", "love-ps3: start");

    lua_State *L = luaL_newstate();
    if (L == NULL) {
        printf("love-ps3: FAIL no Lua state\n");
        write_log("a", "love-ps3: FAIL no Lua state");
        wait_for_quit();
        return 1;
    }

    luaL_openlibs(L);
    lua_runtime_open(L);

    lua_pushstring(L, INSTALL_ROOT);
    lua_setglobal(L, "INSTALL_ROOT");

    lua_pushcfunction(L, luaopen_love_platform);
    lua_call(L, 0, 0);

    lua_pushcfunction(L, luaopen_love_graphics);
    lua_call(L, 0, 0);

    lua_pushcfunction(L, luaopen_love_data);
    lua_call(L, 0, 0);

    lua_pushcfunction(L, luaopen_love_audio);
    lua_call(L, 0, 0);

    lua_pushcfunction(L, traceback);
    int status = luaL_loadfile(L, INSTALL_ROOT "boot.lua");
    if (status == 0)
        status = lua_pcall(L, 0, 0, -2);

    const char *message = status != 0 ? lua_tostring(L, -1) : "clean exit";

    printf("love-ps3: boot ended status=%d %s\n", status, message);
    write_log("a", "love-ps3: boot ended:");
    write_log("a", message);

    if (status != 0)
        wait_for_quit();

    audio_close();
    gfx_close();
    return status == 0 ? 0 : 1;
}
