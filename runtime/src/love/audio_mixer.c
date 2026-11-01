/*
 * Software mixer. The audio port takes only 48 kHz float blocks, and a missed block clicks, so one
 * thread only copies finished blocks to the port, and a second one decodes and mixes ahead of it.
 * The game keeps five music tracks playing at once and fades between them. A silent voice skips
 * decoding and only moves its position, which saves most of the PPE time that would cost.
 *
 * Lua's thread starts and changes voices, and the mixer thread ends them. Each field has one
 * writer, so voices need no lock.
 */
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <audio/audio.h>
#include <sys/event_queue.h>
#include <sys/thread.h>
#include <vorbis/vorbisfile.h>

#include "audio_mixer.h"

#define OUTPUT_RATE      48000.0
#define OUTPUT_CHANNELS  2
#define BLOCK_FRAMES     AUDIO_BLOCK_SAMPLES
#define BLOCK_SAMPLES    (BLOCK_FRAMES * OUTPUT_CHANNELS)
#define BLOCK_BYTES      (BLOCK_SAMPLES * sizeof(f32))
#define PORT_BLOCKS      8
#define RING_BLOCKS      16
#define MAX_VOICES       32
#define STREAM_FRAMES    4096
#define SILENT_VOLUME    0.0005f
#define THREAD_STACK     (256 * 1024)
#define FEEDER_PRIORITY  900
#define MIXER_PRIORITY   1000
#define WAIT_MICROSECONDS 20000

/* Past this size a sound streams from memory, so a long sound never stalls the mixer's decoding. */
#define STREAM_BYTES     (64 * 1024)

struct AudioClip {
    AudioClip *next;
    char *path;
    u8 *bytes;
    size_t size;
    bool stream;
    bool decoded;
    f32 *frames;
    s64 frame_count;
    int channels;
    double rate;
};

enum { VOICE_FREE, VOICE_STARTING, VOICE_PLAYING };

typedef struct {
    const AudioClip *clip;
    size_t position;
} MemoryFile;

typedef struct {
    volatile u32 state;
    volatile u32 generation;
    volatile u32 stop_requested;
    volatile f32 volume;
    volatile f32 pitch;
    AudioClip *clip;
    double position;
    double rate;
    OggVorbis_File file;
    MemoryFile memory;
    bool file_open;
    bool needs_seek;
    bool file_ended;
    int channels;
    s64 total_frames;
    s64 buffer_start;
    int buffer_frames;
} Voice;

static Voice voices[MAX_VOICES];
static f32 stream_buffers[MAX_VOICES][STREAM_FRAMES * OUTPUT_CHANNELS];
static f32 ring[RING_BLOCKS][BLOCK_SAMPLES] __attribute__((aligned(128)));
static volatile u32 ring_write;
static volatile u32 ring_read;
static volatile bool running;
static bool opened;
static AudioClip *clips;
static audioPortConfig port_config;
static u32 port_number;
static sys_event_queue_t notify_queue;
static sys_ipc_key_t notify_key;
static sys_ppu_thread_t feeder_id;
static sys_ppu_thread_t mixer_id;
static u32 underruns;

/* ------------------------------------------------------------------ Vorbis, from memory */

static size_t memory_read(void *target, size_t size, size_t count, void *source)
{
    MemoryFile *memory = source;
    size_t left = memory->clip->size - memory->position;
    size_t items = size == 0 ? 0 : left / size;

    if (items > count)
        items = count;
    memcpy(target, memory->clip->bytes + memory->position, items * size);
    memory->position += items * size;
    return items;
}

static int memory_seek(void *source, ogg_int64_t offset, int origin)
{
    MemoryFile *memory = source;
    ogg_int64_t position = offset;

    if (origin == SEEK_CUR)
        position += (ogg_int64_t)memory->position;
    else if (origin == SEEK_END)
        position += (ogg_int64_t)memory->clip->size;

    if (position < 0 || position > (ogg_int64_t)memory->clip->size)
        return -1;
    memory->position = (size_t)position;
    return 0;
}

static long memory_tell(void *source)
{
    return (long)((MemoryFile *)source)->position;
}

static const ov_callbacks MEMORY_CALLBACKS = { memory_read, memory_seek, NULL, memory_tell };

static int open_vorbis(const AudioClip *clip, MemoryFile *memory, OggVorbis_File *file)
{
    memory->clip = clip;
    memory->position = 0;
    return ov_open_callbacks(memory, file, NULL, 0, MEMORY_CALLBACKS);
}

/* Copies decoded planes into interleaved stereo. A mono sound plays on both sides. */
static void write_stereo(f32 *target, f32 **planes, int channels, long count)
{
    for (long i = 0; i < count; i++) {
        target[i * 2] = planes[0][i];
        target[i * 2 + 1] = planes[channels > 1 ? 1 : 0][i];
    }
}

static void decode_whole_clip(AudioClip *clip)
{
    OggVorbis_File file;
    MemoryFile memory;
    f32 **planes;
    int section;
    long got;
    s64 written = 0;

    clip->decoded = true;
    if (open_vorbis(clip, &memory, &file) != 0) {
        printf("audio: cannot read %s\n", clip->path);
        return;
    }

    const vorbis_info *info = ov_info(&file, -1);
    s64 total = ov_pcm_total(&file, -1);
    f32 *frames = malloc((size_t)total * OUTPUT_CHANNELS * sizeof(f32));

    if (frames != NULL) {
        while (written < total && (got = ov_read_float(&file, &planes, 4096, &section)) > 0) {
            if (got > total - written)
                got = (long)(total - written);
            write_stereo(frames + written * OUTPUT_CHANNELS, planes, info->channels, got);
            written += got;
        }
        clip->rate = info->rate;
        clip->frame_count = written;
        clip->frames = frames;
    }
    ov_clear(&file);
}

/* ------------------------------------------------------------------ one voice */

static void end_voice(Voice *voice)
{
    if (voice->file_open) {
        ov_clear(&voice->file);
        voice->file_open = false;
    }
    voice->stop_requested = 0;
    __asm__ volatile("lwsync");
    voice->state = VOICE_FREE;
}

static bool begin_voice(Voice *voice)
{
    AudioClip *clip = voice->clip;

    voice->position = 0.0;
    voice->buffer_start = 0;
    voice->buffer_frames = 0;
    voice->file_ended = false;
    voice->needs_seek = false;

    if (!clip->stream) {
        if (!clip->decoded)
            decode_whole_clip(clip);
        voice->rate = clip->rate;
        voice->total_frames = clip->frame_count;
        return clip->frames != NULL;
    }

    if (open_vorbis(clip, &voice->memory, &voice->file) != 0) {
        printf("audio: cannot read %s\n", clip->path);
        return false;
    }

    voice->file_open = true;
    voice->channels = ov_info(&voice->file, -1)->channels;
    voice->rate = ov_info(&voice->file, -1)->rate;
    voice->total_frames = ov_pcm_total(&voice->file, -1);
    return true;
}

/* Keeps the last frame, which the next read interpolates from, then fills the buffer behind it. */
static void refill_stream(Voice *voice, f32 *buffer)
{
    f32 **planes;
    int section;

    if (voice->buffer_frames > 0) {
        buffer[0] = buffer[(voice->buffer_frames - 1) * 2];
        buffer[1] = buffer[(voice->buffer_frames - 1) * 2 + 1];
        voice->buffer_start += voice->buffer_frames - 1;
        voice->buffer_frames = 1;
    }

    while (voice->buffer_frames < STREAM_FRAMES) {
        long got = ov_read_float(&voice->file, &planes, STREAM_FRAMES - voice->buffer_frames, &section);

        if (got <= 0) {
            voice->file_ended = true;
            return;
        }
        write_stereo(buffer + voice->buffer_frames * 2, planes, voice->channels, got);
        voice->buffer_frames += (int)got;
    }
}

static void seek_stream(Voice *voice)
{
    s64 target = (s64)voice->position;

    voice->needs_seek = false;
    voice->buffer_frames = 0;
    voice->buffer_start = target;
    voice->file_ended = ov_pcm_seek(&voice->file, target) != 0;
}

/* Returns false once the voice has played to its end. */
static bool mix_stream(Voice *voice, f32 *target, int index)
{
    f32 *buffer = stream_buffers[index];
    f32 volume = voice->volume;
    double step = voice->rate / OUTPUT_RATE * voice->pitch;

    if (volume <= SILENT_VOLUME) {
        voice->position += step * BLOCK_FRAMES;
        voice->needs_seek = true;
        return voice->position < (double)voice->total_frames;
    }

    if (voice->needs_seek)
        seek_stream(voice);

    for (int i = 0; i < BLOCK_FRAMES; i++) {
        s64 whole = (s64)voice->position;

        while (whole + 1 >= voice->buffer_start + voice->buffer_frames && !voice->file_ended)
            refill_stream(voice, buffer);

        s64 offset = whole - voice->buffer_start;
        if (offset < 0 || offset >= voice->buffer_frames)
            return false;

        s64 next = offset + 1 < voice->buffer_frames ? offset + 1 : offset;
        f32 blend = (f32)(voice->position - (double)whole);

        target[i * 2] += volume * (buffer[offset * 2] + blend * (buffer[next * 2] - buffer[offset * 2]));
        target[i * 2 + 1] += volume * (buffer[offset * 2 + 1] + blend * (buffer[next * 2 + 1] - buffer[offset * 2 + 1]));
        voice->position += step;
    }
    return true;
}

static bool mix_clip(Voice *voice, f32 *target)
{
    const f32 *frames = voice->clip->frames;
    s64 count = voice->total_frames;
    f32 volume = voice->volume;
    double step = voice->rate / OUTPUT_RATE * voice->pitch;

    for (int i = 0; i < BLOCK_FRAMES; i++) {
        s64 whole = (s64)voice->position;

        if (whole >= count)
            return false;

        s64 next = whole + 1 < count ? whole + 1 : whole;
        f32 blend = (f32)(voice->position - (double)whole);

        target[i * 2] += volume * (frames[whole * 2] + blend * (frames[next * 2] - frames[whole * 2]));
        target[i * 2 + 1] += volume * (frames[whole * 2 + 1] + blend * (frames[next * 2 + 1] - frames[whole * 2 + 1]));
        voice->position += step;
    }
    return true;
}

static void mix_block(f32 *target)
{
    memset(target, 0, BLOCK_BYTES);

    for (int index = 0; index < MAX_VOICES; index++) {
        Voice *voice = &voices[index];
        u32 state = voice->state;

        if (state == VOICE_FREE)
            continue;

        if (voice->stop_requested) {
            end_voice(voice);
            continue;
        }

        if (state == VOICE_STARTING) {
            if (!begin_voice(voice)) {
                end_voice(voice);
                continue;
            }
            voice->state = VOICE_PLAYING;
        }

        bool playing = voice->clip->stream ? mix_stream(voice, target, index) : mix_clip(voice, target);
        if (!playing)
            end_voice(voice);
    }

    for (int i = 0; i < BLOCK_SAMPLES; i++) {
        if (target[i] > 1.0f) target[i] = 1.0f;
        if (target[i] < -1.0f) target[i] = -1.0f;
    }
}

/* ------------------------------------------------------------------ threads */

static void mixer_thread(void *argument)
{
    (void)argument;

    while (running) {
        if (ring_write - ring_read >= RING_BLOCKS) {
            usleep(1000);
            continue;
        }
        mix_block(ring[ring_write % RING_BLOCKS]);
        __asm__ volatile("lwsync");
        ring_write++;
    }
    sysThreadExit(0);
}

/* Does nothing but copy: mixing or decoding here would miss the 5.33 ms deadline. */
static void feeder_thread(void *argument)
{
    (void)argument;
    sys_event_t event;
    f32 *port = (f32 *)(u64)port_config.audioDataStart;

    while (running) {
        if (sysEventQueueReceive(notify_queue, &event, WAIT_MICROSECONDS) != 0)
            continue;

        u64 playing = *(vu64 *)(u64)port_config.readIndex;
        f32 *target = port + ((playing + 1) % PORT_BLOCKS) * BLOCK_SAMPLES;

        if (ring_write == ring_read) {
            memset(target, 0, BLOCK_BYTES);
            underruns++;
            continue;
        }
        memcpy(target, ring[ring_read % RING_BLOCKS], BLOCK_BYTES);
        __asm__ volatile("lwsync");
        ring_read++;
    }
    sysThreadExit(0);
}

int audio_open(void)
{
    audioPortParam parameters;

    if (audioInit() != 0)
        return -1;

    parameters.numChannels = AUDIO_PORT_2CH;
    parameters.numBlocks = AUDIO_BLOCK_8;
    parameters.attrib = 0;
    parameters.level = 1.0f;

    if (audioPortOpen(&parameters, &port_number) != 0)
        return -1;
    if (audioGetPortConfig(port_number, &port_config) != 0)
        return -1;
    if (audioCreateNotifyEventQueue(&notify_queue, &notify_key) != 0)
        return -1;
    if (audioSetNotifyEventQueue(notify_key) != 0)
        return -1;

    running = true;
    if (sysThreadCreate(&mixer_id, mixer_thread, NULL, MIXER_PRIORITY, THREAD_STACK, 0, "audio mixer") != 0)
        return -1;
    if (sysThreadCreate(&feeder_id, feeder_thread, NULL, FEEDER_PRIORITY, THREAD_STACK, 0, "audio feeder") != 0)
        return -1;
    if (audioPortStart(port_number) != 0)
        return -1;

    opened = true;
    return 0;
}

void audio_close(void)
{
    if (!opened)
        return;
    opened = false;
    running = false;
    /* Long enough for both threads to see the flag, and the port to play out silently. */
    usleep(50000);
    audioPortStop(port_number);
    audioPortClose(port_number);
    audioQuit();
}

/* ------------------------------------------------------------------ Lua's side */

static u8 *read_whole_file(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");

    if (file == NULL)
        return NULL;

    fseek(file, 0, SEEK_END);
    *size = (size_t)ftell(file);
    fseek(file, 0, SEEK_SET);

    u8 *bytes = malloc(*size);
    if (bytes != NULL && fread(bytes, 1, *size, file) != *size) {
        free(bytes);
        bytes = NULL;
    }
    fclose(file);
    return bytes;
}

AudioClip *audio_clip_load(const char *path, bool stream)
{
    for (AudioClip *clip = clips; clip != NULL; clip = clip->next) {
        if (strcmp(clip->path, path) == 0)
            return clip;
    }

    AudioClip *clip = calloc(1, sizeof(AudioClip));
    if (clip == NULL)
        return NULL;

    clip->bytes = read_whole_file(path, &clip->size);
    clip->path = strdup(path);
    if (clip->bytes == NULL || clip->path == NULL) {
        free(clip);
        return NULL;
    }

    clip->stream = stream || clip->size > STREAM_BYTES;
    clip->next = clips;
    clips = clip;
    return clip;
}

/* A handle holds the voice's index and its generation, so an old handle cannot touch a new sound. */
static Voice *voice_of(AudioVoice handle)
{
    if (handle == AUDIO_NO_VOICE)
        return NULL;

    Voice *voice = &voices[handle & 0xFF];
    if (voice->generation != (u32)(handle >> 8) || voice->state == VOICE_FREE)
        return NULL;
    return voice;
}

AudioVoice audio_voice_start(AudioClip *clip, f32 volume, f32 pitch)
{
    if (!opened || clip == NULL)
        return AUDIO_NO_VOICE;

    for (int index = 0; index < MAX_VOICES; index++) {
        Voice *voice = &voices[index];

        if (voice->state != VOICE_FREE)
            continue;

        voice->generation = (voice->generation + 1) & 0xFFFFF;
        voice->clip = clip;
        voice->volume = volume;
        voice->pitch = pitch;
        voice->stop_requested = 0;
        __asm__ volatile("lwsync");
        voice->state = VOICE_STARTING;
        return (AudioVoice)((voice->generation << 8) | (u32)index);
    }
    return AUDIO_NO_VOICE;
}

void audio_voice_set_volume(AudioVoice handle, f32 volume)
{
    Voice *voice = voice_of(handle);

    if (voice != NULL)
        voice->volume = volume;
}

void audio_voice_set_pitch(AudioVoice handle, f32 pitch)
{
    Voice *voice = voice_of(handle);

    if (voice != NULL)
        voice->pitch = pitch;
}

void audio_voice_stop(AudioVoice handle)
{
    Voice *voice = voice_of(handle);

    if (voice != NULL)
        voice->stop_requested = 1;
}

bool audio_voice_is_playing(AudioVoice handle)
{
    Voice *voice = voice_of(handle);

    return voice != NULL && !voice->stop_requested;
}

u32 audio_underruns(void)
{
    return underruns;
}

void audio_stop_all(void)
{
    for (int index = 0; index < MAX_VOICES; index++) {
        if (voices[index].state != VOICE_FREE)
            voices[index].stop_requested = 1;
    }
}
