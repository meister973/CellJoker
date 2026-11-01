/*
 * `bit` library, LuaJIT-compatible surface.
 *
 * The game does `require "bit"`, which on desktop resolves to LuaJIT's built-in.
 * We ship vanilla Lua 5.1, so it has to exist as a C module with the same
 * 32-bit wrap-around semantics. Implemented here rather than pulled in so the
 * behaviour is ours to test.
 */
#include <stdint.h>

#include <lua.h>
#include <lauxlib.h>

typedef int32_t s32;
typedef uint32_t u32;

/* (s32)double saturates on PowerPC (fctiwz). Adding 2^52 + 2^51 puts the rounded
 * integer in the low mantissa bits, which matches LuaJIT's wrap and rounding. */
static u32 arg_to_u32(lua_State *L, int index)
{
    union {
        lua_Number number;
        uint64_t bits;
    } normalized;

    normalized.number = luaL_checknumber(L, index) + 6755399441055744.0;
    return (u32)normalized.bits;
}

static void push_s32(lua_State *L, u32 value)
{
    lua_pushnumber(L, (lua_Number)(s32)value);
}

static int bit_band(lua_State *L)
{
    int argc = lua_gettop(L);
    u32 result = argc ? arg_to_u32(L, 1) : 0;

    for (int i = 2; i <= argc; i++)
        result &= arg_to_u32(L, i);

    push_s32(L, result);
    return 1;
}

static int bit_bor(lua_State *L)
{
    int argc = lua_gettop(L);
    u32 result = argc ? arg_to_u32(L, 1) : 0;

    for (int i = 2; i <= argc; i++)
        result |= arg_to_u32(L, i);

    push_s32(L, result);
    return 1;
}

static int bit_bxor(lua_State *L)
{
    int argc = lua_gettop(L);
    u32 result = argc ? arg_to_u32(L, 1) : 0;

    for (int i = 2; i <= argc; i++)
        result ^= arg_to_u32(L, i);

    push_s32(L, result);
    return 1;
}

static int bit_bnot(lua_State *L)
{
    push_s32(L, ~arg_to_u32(L, 1));
    return 1;
}

/* Shift counts are masked to 5 bits, as LuaJIT does, so a count of 32 is a no-op. */
static int bit_lshift(lua_State *L)
{
    u32 value = arg_to_u32(L, 1);
    u32 count = arg_to_u32(L, 2) & 31u;

    push_s32(L, value << count);
    return 1;
}

static int bit_rshift(lua_State *L)
{
    u32 value = arg_to_u32(L, 1);
    u32 count = arg_to_u32(L, 2) & 31u;

    push_s32(L, value >> count);
    return 1;
}

static int bit_arshift(lua_State *L)
{
    s32 value = (s32)arg_to_u32(L, 1);
    u32 count = arg_to_u32(L, 2) & 31u;

    push_s32(L, (u32)(value >> count));
    return 1;
}

static int bit_rol(lua_State *L)
{
    u32 value = arg_to_u32(L, 1);
    u32 count = arg_to_u32(L, 2) & 31u;

    push_s32(L, count ? (value << count) | (value >> (32u - count)) : value);
    return 1;
}

static int bit_ror(lua_State *L)
{
    u32 value = arg_to_u32(L, 1);
    u32 count = arg_to_u32(L, 2) & 31u;

    push_s32(L, count ? (value >> count) | (value << (32u - count)) : value);
    return 1;
}

static int bit_bswap(lua_State *L)
{
    u32 value = arg_to_u32(L, 1);
    u32 swapped = ((value & 0x000000ffu) << 24) |
                  ((value & 0x0000ff00u) << 8) |
                  ((value & 0x00ff0000u) >> 8) |
                  ((value & 0xff000000u) >> 24);

    push_s32(L, swapped);
    return 1;
}

static int bit_tobit(lua_State *L)
{
    push_s32(L, arg_to_u32(L, 1));
    return 1;
}

static int bit_tohex(lua_State *L)
{
    u32 value = arg_to_u32(L, 1);
    int digits = lua_gettop(L) >= 2 ? (int)luaL_checknumber(L, 2) : 8;
    char buffer[16];
    /* A negative digit count means uppercase, as in LuaJIT. */
    const char *hex = digits < 0 ? "0123456789ABCDEF" : "0123456789abcdef";

    if (digits < 0) digits = -digits;
    if (digits < 1) digits = 1;
    if (digits > 8) digits = 8;

    for (int i = digits - 1; i >= 0; i--) {
        buffer[i] = hex[value & 0xfu];
        value >>= 4;
    }
    buffer[digits] = '\0';

    lua_pushstring(L, buffer);
    return 1;
}

static const luaL_Reg bit_library[] = {
    { "band",   bit_band   },
    { "bor",    bit_bor    },
    { "bxor",   bit_bxor   },
    { "bnot",   bit_bnot   },
    { "lshift", bit_lshift },
    { "rshift", bit_rshift },
    { "arshift", bit_arshift },
    { "rol",    bit_rol    },
    { "ror",    bit_ror    },
    { "bswap",  bit_bswap  },
    { "tobit",  bit_tobit  },
    { "tohex",  bit_tohex  },
    { NULL, NULL }
};

int luaopen_bit(lua_State *L)
{
    luaL_register(L, "bit", bit_library);
    return 1;
}
