/*
 * `love_platform`: the few LV2 services the Lua side of the runtime needs. Everything
 * else in love.filesystem and love.timer is plain Lua on top of these.
 */
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>

#include <lua.h>
#include <lauxlib.h>

#include <ppu-lv2.h>
#include <sys/file.h>
#include <sys/systime.h>
#include <sysutil/sysutil.h>

#include "lua_platform.h"

static volatile bool exit_requested;

static void on_system_event(u64 status, u64 param, void *user_data)
{
    (void)param;
    (void)user_data;
    if (status == SYSUTIL_EXIT_GAME)
        exit_requested = true;
}

bool love_platform_poll_exit(void)
{
    sysUtilCheckCallback();
    return exit_requested;
}

static int platform_time(lua_State *L)
{
    u64 seconds = 0;
    u64 nanoseconds = 0;

    sysGetCurrentTime(&seconds, &nanoseconds);
    lua_pushnumber(L, (double)seconds + (double)nanoseconds * 1e-9);
    return 1;
}

static int platform_sleep(lua_State *L)
{
    double seconds = luaL_checknumber(L, 1);

    if (seconds > 0)
        sysUsleep((u32)(seconds * 1e6));
    return 0;
}

/* Returns kind ("file" or "directory") and size, or nil when the path is absent. */
static int platform_stat(lua_State *L)
{
    sysFSStat info;

    if (sysLv2FsStat(luaL_checkstring(L, 1), &info) != 0) {
        lua_pushnil(L);
        return 1;
    }

    lua_pushstring(L, (info.st_mode & S_IFDIR) ? "directory" : "file");
    lua_pushnumber(L, (double)info.st_size);
    return 2;
}

static int platform_list(lua_State *L)
{
    s32 directory;
    sysFSDirent entry;
    u64 read_size;
    int count = 0;

    lua_newtable(L);
    if (sysLv2FsOpenDir(luaL_checkstring(L, 1), &directory) != 0)
        return 1;

    while (sysLv2FsReadDir(directory, &entry, &read_size) == 0 && read_size != 0) {
        if (strcmp(entry.d_name, ".") == 0 || strcmp(entry.d_name, "..") == 0)
            continue;
        lua_pushstring(L, entry.d_name);
        lua_rawseti(L, -2, ++count);
    }

    sysLv2FsCloseDir(directory);
    return 1;
}

static int platform_mkdir(lua_State *L)
{
    lua_pushboolean(L, sysLv2FsMkdir(luaL_checkstring(L, 1), 0777) == 0);
    return 1;
}

static int platform_remove(lua_State *L)
{
    lua_pushboolean(L, sysLv2FsUnlink(luaL_checkstring(L, 1)) == 0);
    return 1;
}

static int platform_exit_requested(lua_State *L)
{
    lua_pushboolean(L, love_platform_poll_exit());
    return 1;
}

static const luaL_Reg platform_library[] = {
    { "exit_requested", platform_exit_requested },
    { "time",   platform_time   },
    { "sleep",  platform_sleep  },
    { "stat",   platform_stat   },
    { "list",   platform_list   },
    { "mkdir",  platform_mkdir  },
    { "remove", platform_remove },
    { NULL, NULL }
};

int luaopen_love_platform(lua_State *L)
{
    sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, on_system_event, NULL);
    luaL_register(L, "love_platform", platform_library);
    lua_pad_register(L);
    return 1;
}
