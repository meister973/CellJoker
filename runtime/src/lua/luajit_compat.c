/*
 * LuaJIT behaviour the shipped game relies on, on top of stock Lua 5.1.
 *
 * pseudorandom() seeds with a fraction, and Lua 5.1 truncates that to 0, so every
 * "random" result would be identical. The generator is LuaJIT's Tausworthe PRNG, so
 * a given seed gives the same run as on PC. tests/lua_compat proves the match.
 */
#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include <lua.h>
#include <lauxlib.h>

#include "lua_runtime.h"

typedef struct {
    uint64_t state[4];
} PrngState;

typedef union {
    double number;
    uint64_t bits;
} DoubleBits;

#define TW223_STEP(prng, value, result, index, k, q, s) \
    value = prng->state[index]; \
    value = (((value << q) ^ value) >> (k - s)) ^ ((value & ((uint64_t)(int64_t)-1 << (64 - k))) << s); \
    result ^= value; \
    prng->state[index] = value;

static uint64_t prng_next(PrngState *prng)
{
    uint64_t value;
    uint64_t result = 0;

    TW223_STEP(prng, value, result, 0, 63, 31, 18)
    TW223_STEP(prng, value, result, 1, 58, 19, 28)
    TW223_STEP(prng, value, result, 2, 55, 24, 7)
    TW223_STEP(prng, value, result, 3, 47, 21, 8)
    return result;
}

static void prng_seed(PrngState *prng, double seed)
{
    uint32_t shifts = 0x11090601;
    DoubleBits mixed;

    for (int i = 0; i < 4; i++) {
        uint64_t minimum = (uint64_t)1 << (shifts & 255);

        shifts >>= 8;
        seed = seed * 3.14159265358979323846 + 2.7182818284590452354;
        mixed.number = seed;
        if (mixed.bits < minimum)
            mixed.bits += minimum;
        prng->state[i] = mixed.bits;
    }

    for (int i = 0; i < 10; i++)
        (void)prng_next(prng);
}

static int math_randomseed(lua_State *L)
{
    prng_seed(lua_touserdata(L, lua_upvalueindex(1)), luaL_checknumber(L, 1));
    return 0;
}

static int math_random(lua_State *L)
{
    PrngState *prng = lua_touserdata(L, lua_upvalueindex(1));
    int argument_count = lua_gettop(L);
    DoubleBits unit;
    double value;

    unit.bits = (prng_next(prng) & 0x000fffffffffffffULL) | 0x3ff0000000000000ULL;
    value = unit.number - 1.0;

    if (argument_count == 1) {
        value = floor(value * luaL_checknumber(L, 1)) + 1.0;
    } else if (argument_count >= 2) {
        double low = luaL_checknumber(L, 1);
        double high = luaL_checknumber(L, 2);

        value = floor(value * (high - low + 1.0)) + low;
    }

    lua_pushnumber(L, value);
    return 1;
}

static int math_log_with_base(lua_State *L)
{
    double x = luaL_checknumber(L, 1);

    if (lua_gettop(L) < 2) {
        lua_pushnumber(L, log(x));
        return 1;
    }

    double base = luaL_checknumber(L, 2);
    double result = log2(x) * (1.0 / log2(base));
    double nearest = floor(result + 0.5);

    /* newlib's log2 lands just under exact powers, and number_format floors this
     * value, so 1000 would count as two digits. Exact powers must stay exact. */
    if (fabs(result - nearest) < 1e-9 && pow(base, nearest) == x)
        result = nearest;

    lua_pushnumber(L, result);
    return 1;
}

static const char *skip_conversion_flags(const char *cursor)
{
    while (*cursor != '\0' && strchr("-+ #0123456789.", *cursor) != NULL)
        cursor++;
    return cursor;
}

static void stringify_argument(lua_State *L, int index)
{
    if (lua_type(L, index) == LUA_TSTRING || lua_type(L, index) == LUA_TNUMBER)
        return;

    lua_getglobal(L, "tostring");
    lua_pushvalue(L, index);
    lua_call(L, 1, 1);
    lua_replace(L, index);
}

/* LuaJIT's %s applies tostring; stock 5.1 raises an error on nil, booleans and tables. */
static int string_format(lua_State *L)
{
    const char *cursor = luaL_checkstring(L, 1);
    int argument = 1;
    int top = lua_gettop(L);

    while ((cursor = strchr(cursor, '%')) != NULL) {
        cursor++;
        if (*cursor == '%') {
            cursor++;
            continue;
        }
        cursor = skip_conversion_flags(cursor);
        argument++;
        if (*cursor == 's' && argument <= top)
            stringify_argument(L, argument);
    }

    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    lua_call(L, top, 1);
    return 1;
}

static void override_math(lua_State *L)
{
    PrngState *prng = lua_newuserdata(L, sizeof(PrngState));

    prng_seed(prng, 0.0);

    lua_getglobal(L, "math");

    lua_pushvalue(L, -2);
    lua_pushcclosure(L, math_random, 1);
    lua_setfield(L, -2, "random");

    lua_pushvalue(L, -2);
    lua_pushcclosure(L, math_randomseed, 1);
    lua_setfield(L, -2, "randomseed");

    lua_pushcfunction(L, math_log_with_base);
    lua_setfield(L, -2, "log");

    lua_pop(L, 2);
}

static void override_string(lua_State *L)
{
    lua_getglobal(L, "string");
    lua_getfield(L, -1, "format");
    lua_pushcclosure(L, string_format, 1);
    lua_setfield(L, -2, "format");
    lua_pop(L, 1);
}

int luaopen_luajit_compat(lua_State *L)
{
    override_math(L);
    override_string(L);
    return 0;
}
