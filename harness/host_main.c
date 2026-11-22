/*
 * Host harness entry point: the same Lua 5.1 state that love-ps3/source/main.c builds,
 * with the PS3 services replaced by host ones, then boot.lua from the install folder.
 *
 * Time is simulated: each present() advances the clock by one frame, so a run is
 * deterministic and its per-frame cost reads like a frame on the console, whatever
 * the host speed.
 *
 *   harness <install root with trailing slash> <host_love_native.lua>
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "lua_platform.h"

#ifdef HARNESS_CALLGRIND
#include <valgrind/callgrind.h>
#endif
#ifdef HARNESS_LUAJIT
/* LuaJIT has bit and its own PRNG built in: this is the interpreter the PC game runs on. */
#include "luajit.h"
#else
#include "lua_runtime.h"
#endif

/* The count hook fires every HOOK_STEP VM instructions, in every coroutine. */
#define HOOK_STEP 1000

static double simulated_seconds;
/* Each read of the clock costs time on the PS3 too. Without a step, nuGC (misc_functions.lua)
 * sees no time pass and always runs its 1000-step maximum. HARNESS_CLOCK_STEP_US sets it. */
static double clock_step_seconds = 2e-6;
static unsigned long long hook_ticks;

static void count_instructions(lua_State *L, lua_Debug *ar)
{
    (void)L;
    (void)ar;
    hook_ticks++;
}

static int harness_instructions(lua_State *L)
{
    lua_pushnumber(L, (double)hook_ticks * HOOK_STEP);
    return 1;
}

/* Process CPU time, so host scheduling noise stays out of the numbers. */
static int harness_cpu(lua_State *L)
{
    struct timespec now;

    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &now);
    lua_pushnumber(L, (double)now.tv_sec + (double)now.tv_nsec * 1e-9);
    return 1;
}

static int harness_tick(lua_State *L)
{
    simulated_seconds += luaL_checknumber(L, 1);
    return 0;
}

static int platform_time(lua_State *L)
{
    simulated_seconds += clock_step_seconds;
    lua_pushnumber(L, simulated_seconds);
    return 1;
}

/* Sleeping would only slow the host down: simulated time is already one frame per present. */
static int platform_sleep(lua_State *L)
{
    (void)L;
    return 0;
}

/* Collects callgrind costs only while the driver asks, so boot stays out of the profile. */
static int harness_callgrind(lua_State *L)
{
#ifdef HARNESS_CALLGRIND
    if (lua_toboolean(L, 1))
        CALLGRIND_START_INSTRUMENTATION;
    else
        CALLGRIND_STOP_INSTRUMENTATION;
#else
    (void)L;
#endif
    return 0;
}

static int platform_stat(lua_State *L)
{
    struct stat info;

    if (stat(luaL_checkstring(L, 1), &info) != 0) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushstring(L, S_ISDIR(info.st_mode) ? "directory" : "file");
    lua_pushnumber(L, (double)info.st_size);
    return 2;
}

static int platform_list(lua_State *L)
{
    DIR *directory = opendir(luaL_checkstring(L, 1));
    struct dirent *entry;
    int count = 0;

    lua_newtable(L);
    if (directory == NULL)
        return 1;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        lua_pushstring(L, entry->d_name);
        lua_rawseti(L, -2, ++count);
    }
    closedir(directory);
    return 1;
}

static int platform_mkdir(lua_State *L)
{
    lua_pushboolean(L, mkdir(luaL_checkstring(L, 1), 0777) == 0);
    return 1;
}

static int platform_remove(lua_State *L)
{
    lua_pushboolean(L, unlink(luaL_checkstring(L, 1)) == 0);
    return 1;
}

static int platform_false(lua_State *L)
{
    lua_pushboolean(L, 0);
    return 1;
}

/* No pad is connected: the driver presses the game's buttons through G.FUNCS. */
static int platform_read_pad(lua_State *L)
{
    lua_pushboolean(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    return 5;
}

static const luaL_Reg platform_library[] = {
    { "exit_requested", platform_false    },
    { "time",           platform_time     },
    { "sleep",          platform_sleep    },
    { "stat",           platform_stat     },
    { "list",           platform_list     },
    { "mkdir",          platform_mkdir    },
    { "remove",         platform_remove   },
    { "read_pad",       platform_read_pad },
    { NULL, NULL }
};

static const luaL_Reg harness_library[] = {
    { "instructions", harness_instructions },
    { "cpu",          harness_cpu          },
    { "tick",         harness_tick         },
    { "callgrind",    harness_callgrind    },
    { NULL, NULL }
};

static int traceback(lua_State *L)
{
    lua_getglobal(L, "debug");
    lua_getfield(L, -1, "traceback");
    lua_pushvalue(L, 1);
    lua_pushinteger(L, 2);
    lua_call(L, 2, 1);
    return 1;
}

static int run_file(lua_State *L, const char *path)
{
    lua_pushcfunction(L, traceback);
    int status = luaL_loadfile(L, path);
    if (status == 0)
        status = lua_pcall(L, 0, 0, -2);
    if (status != 0)
        printf("harness: %s failed: %s\n", path, lua_tostring(L, -1));
    lua_settop(L, 0);
    return status;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: harness <install root/> <host_love_native.lua>\n");
        return 2;
    }

    const char *clock_step = getenv("HARNESS_CLOCK_STEP_US");
    if (clock_step != NULL)
        clock_step_seconds = atof(clock_step) * 1e-6;

    lua_State *L = luaL_newstate();
    luaL_openlibs(L);
#ifdef HARNESS_LUAJIT
    /* Interpreter only unless asked: count hooks do not fire inside compiled traces. */
    if (getenv("HARNESS_JIT") == NULL)
        luaJIT_setmode(L, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_OFF);
#else
    lua_runtime_open(L);
#endif
    lua_sethook(L, count_instructions, LUA_MASKCOUNT, HOOK_STEP);

    lua_pushstring(L, argv[1]);
    lua_setglobal(L, "INSTALL_ROOT");

    luaL_register(L, "love_platform", platform_library);
    luaL_register(L, "harness_native", harness_library);
    lua_settop(L, 0);

    lua_pushcfunction(L, luaopen_love_data);
    lua_call(L, 0, 0);

    if (run_file(L, argv[2]) != 0)
        return 1;

    char boot_path[4096];
    snprintf(boot_path, sizeof boot_path, "%sboot.lua", argv[1]);
    int status = run_file(L, boot_path);
    printf("harness: boot ended status=%d\n", status);
    fflush(stdout);
    return status == 0 ? 0 : 1;
}
