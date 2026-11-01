/* love.data.compress and decompress, for the game's save files. LÖVE's "deflate" is raw deflate. */
#include <stdlib.h>
#include <string.h>

#include <lua.h>
#include <lauxlib.h>
#include <zlib.h>

#include "lua_platform.h"

#define RAW_DEFLATE_WINDOW (-15)
#define CHUNK_BYTES (64 * 1024)

/* Runs zlib until the input is gone, and leaves the output on the Lua stack. */
static int run_stream(lua_State *L, z_stream *stream, const char *input, size_t size,
                      int (*step)(z_streamp, int), int flush)
{
    luaL_Buffer output;
    unsigned char *chunk = malloc(CHUNK_BYTES);
    int status;

    if (chunk == NULL)
        return Z_MEM_ERROR;

    stream->next_in = (Bytef *)input;
    stream->avail_in = (uInt)size;
    luaL_buffinit(L, &output);

    do {
        stream->next_out = chunk;
        stream->avail_out = CHUNK_BYTES;
        status = step(stream, flush);
        if (status != Z_OK && status != Z_STREAM_END)
            break;
        luaL_addlstring(&output, (const char *)chunk, CHUNK_BYTES - stream->avail_out);
    } while (status != Z_STREAM_END && (stream->avail_in > 0 || stream->avail_out == 0));

    free(chunk);
    luaL_pushresult(&output);
    return status;
}

static void check_format(lua_State *L)
{
    if (strcmp(luaL_checkstring(L, 2), "deflate") != 0)
        luaL_error(L, "love.data: only deflate is supported");
}

/* compress("string", "deflate", data, level) */
static int data_compress(lua_State *L)
{
    size_t size;
    z_stream stream;

    check_format(L);
    const char *input = luaL_checklstring(L, 3, &size);
    int level = luaL_optint(L, 4, Z_DEFAULT_COMPRESSION);

    memset(&stream, 0, sizeof(stream));
    if (deflateInit2(&stream, level, Z_DEFLATED, RAW_DEFLATE_WINDOW, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return luaL_error(L, "love.data.compress: cannot start");

    int status = run_stream(L, &stream, input, size, deflate, Z_FINISH);
    deflateEnd(&stream);

    if (status != Z_STREAM_END)
        return luaL_error(L, "love.data.compress: failed with %d", status);
    return 1;
}

/* decompress("string", "deflate", data). Bad data raises an error, which the game catches. */
static int data_decompress(lua_State *L)
{
    size_t size;
    z_stream stream;

    check_format(L);
    const char *input = luaL_checklstring(L, 3, &size);

    memset(&stream, 0, sizeof(stream));
    if (inflateInit2(&stream, RAW_DEFLATE_WINDOW) != Z_OK)
        return luaL_error(L, "love.data.decompress: cannot start");

    int status = run_stream(L, &stream, input, size, inflate, Z_NO_FLUSH);
    inflateEnd(&stream);

    if (status != Z_STREAM_END)
        return luaL_error(L, "love.data.decompress: bad data (%d)", status);
    return 1;
}

static const luaL_Reg data_library[] = {
    { "compress",   data_compress   },
    { "decompress", data_decompress },
    { NULL, NULL }
};

int luaopen_love_data(lua_State *L)
{
    luaL_register(L, "love_data_native", data_library);
    return 1;
}
