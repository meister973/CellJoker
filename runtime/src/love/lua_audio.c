/* love.audio over the mixer: the few calls the game's sound manager makes. */
#include <stdio.h>
#include <string.h>

#include <lua.h>
#include <lauxlib.h>

#include "audio_mixer.h"
#include "lua_platform.h"

#define SOURCE_TYPE "love.Source"

typedef struct {
    AudioClip *clip;
    AudioVoice voice;
    f32 volume;
    f32 pitch;
} Source;

static Source *check_source(lua_State *L, int index)
{
    return luaL_checkudata(L, index, SOURCE_TYPE);
}

/* newSource(path, "static" or "stream") */
static int audio_newSource(lua_State *L)
{
    char full_path[320];
    const char *path = luaL_checkstring(L, 1);
    bool stream = strcmp(luaL_optstring(L, 2, "static"), "stream") == 0;

    lua_getglobal(L, "INSTALL_ROOT");
    snprintf(full_path, sizeof(full_path), "%s%s", lua_tostring(L, -1), path);
    lua_pop(L, 1);

    AudioClip *clip = audio_clip_load(full_path, stream);
    if (clip == NULL)
        return luaL_error(L, "love.audio.newSource: cannot load %s", full_path);

    Source *source = lua_newuserdata(L, sizeof(Source));
    source->clip = clip;
    source->voice = AUDIO_NO_VOICE;
    source->volume = 1.0f;
    source->pitch = 1.0f;
    luaL_getmetatable(L, SOURCE_TYPE);
    lua_setmetatable(L, -2);
    return 1;
}

static int source_play(lua_State *L)
{
    Source *source = check_source(L, 1);

    if (!audio_voice_is_playing(source->voice))
        source->voice = audio_voice_start(source->clip, source->volume, source->pitch);
    return 0;
}

static int audio_stop(lua_State *L)
{
    if (lua_isnoneornil(L, 1)) {
        audio_stop_all();
        return 0;
    }
    audio_voice_stop(check_source(L, 1)->voice);
    return 0;
}

static int source_setVolume(lua_State *L)
{
    Source *source = check_source(L, 1);

    source->volume = (f32)luaL_checknumber(L, 2);
    audio_voice_set_volume(source->voice, source->volume);
    return 0;
}

static int source_setPitch(lua_State *L)
{
    Source *source = check_source(L, 1);

    source->pitch = (f32)luaL_checknumber(L, 2);
    audio_voice_set_pitch(source->voice, source->pitch);
    return 0;
}

static int source_isPlaying(lua_State *L)
{
    lua_pushboolean(L, audio_voice_is_playing(check_source(L, 1)->voice));
    return 1;
}

/* Clips stay cached for the next play, so there is nothing to give back. */
static int source_release(lua_State *L)
{
    check_source(L, 1);
    return 0;
}

static const luaL_Reg source_methods[] = {
    { "play",      source_play      },
    { "stop",      audio_stop       },
    { "setVolume", source_setVolume },
    { "setPitch",  source_setPitch  },
    { "isPlaying", source_isPlaying },
    { "release",   source_release   },
    { NULL, NULL }
};

static int audio_underrun_count(lua_State *L)
{
    lua_pushnumber(L, audio_underruns());
    return 1;
}

static const luaL_Reg audio_library[] = {
    { "underruns", audio_underrun_count },
    { "newSource", audio_newSource },
    { "play",      source_play     },
    { "stop",      audio_stop      },
    { NULL, NULL }
};

int luaopen_love_audio(lua_State *L)
{
    luaL_newmetatable(L, SOURCE_TYPE);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    luaL_register(L, NULL, source_methods);
    lua_pop(L, 1);

    /* A console with no audio still plays the game, silently: voices just never start. */
    if (audio_open() != 0)
        printf("love-ps3: audio is off, the port did not open\n");

    luaL_register(L, "love_audio_native", audio_library);
    return 1;
}
