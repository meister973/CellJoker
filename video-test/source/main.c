/*
 * Phase 3 gate: RSX bring-up.
 *
 * Initialises the display at whatever mode the system reports, allocates
 * double-buffered colour plus a Z24S8 depth surface in RSX-local memory, and
 * cycles a full-screen clear colour for a few hundred vsynced flips.
 *
 * Deliberately no 3D, no textures, no shaders: if this shows a pulsing colour,
 * then rsxInit/videoConfigure/display-buffer registration/flip protocol are all
 * correct, and every later bug is a bug in something else.
 */
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <unistd.h>

#include <ppu-types.h>
#include <rsx/rsx.h>
#include <rsx/gcm_sys.h>
#include <sysutil/video.h>
#include <sys/process.h>

#include "rsx_ring.h"

SYS_PROCESS_PARAM(1001, 0x100000)

#define CB_SIZE       0x80000
#define HOST_SIZE     (8 * 1024 * 1024)
#define FRAME_COUNT   2
#define TEST_FRAMES   600

static gcmContextData *gcm_context;

static u32 display_width;
static u32 display_height;
static u32 color_pitch;
static u32 depth_pitch;

static u32 color_offset[FRAME_COUNT];
static void *color_buffer[FRAME_COUNT];
static u32 depth_offset;
static void *depth_buffer;

static int init_video(void)
{
    videoState state;

    if (videoGetState(0, 0, &state) != 0) {
        printf("video-test: FAIL videoGetState\n");
        return -1;
    }

    videoResolution resolution;

    if (videoGetResolution(state.displayMode.resolution, &resolution) != 0) {
        printf("video-test: FAIL videoGetResolution\n");
        return -1;
    }

    display_width = resolution.width;
    display_height = resolution.height;

    videoConfiguration config;
    memset(&config, 0, sizeof(config));
    config.resolution = state.displayMode.resolution;
    config.format = VIDEO_BUFFER_FORMAT_XRGB;
    config.aspect = state.displayMode.aspect;
    config.pitch = display_width * 4;

    if (videoConfigure(0, &config, NULL, 0) != 0) {
        printf("video-test: FAIL videoConfigure\n");
        return -1;
    }

    printf("video-test: resolution %ux%u (id 0x%02x) aspect 0x%02x\n",
           display_width, display_height,
           state.displayMode.resolution, state.displayMode.aspect);
    return 0;
}

static void set_surface(u32 index)
{
    gcmSurface surface;

    memset(&surface, 0, sizeof(surface));

    surface.type = GCM_SURFACE_TYPE_LINEAR;
    surface.antiAlias = GCM_SURFACE_CENTER_1;
    surface.colorFormat = GCM_SURFACE_X8R8G8B8;
    surface.colorTarget = GCM_SURFACE_TARGET_0;

    surface.colorLocation[0] = GCM_LOCATION_RSX;
    surface.colorOffset[0] = color_offset[index];
    surface.colorPitch[0] = color_pitch;

    for (u32 i = 1; i < GCM_MAX_MRT_COUNT; i++) {
        surface.colorLocation[i] = GCM_LOCATION_RSX;
        surface.colorOffset[i] = color_offset[index];
        surface.colorPitch[i] = 64;
    }

    surface.depthFormat = GCM_SURFACE_ZETA_Z24S8;
    surface.depthLocation = GCM_LOCATION_RSX;
    surface.depthOffset = depth_offset;
    surface.depthPitch = depth_pitch;

    surface.width = display_width;
    surface.height = display_height;
    surface.x = 0;
    surface.y = 0;

    rsxSetSurface(gcm_context, &surface);
}

static void flip(u32 index)
{
    while (gcmGetFlipStatus() != 0)
        usleep(200);

    gcmResetFlipStatus();

    gcmSetFlip(gcm_context, (u8)index);
    rsxFlushBuffer(gcm_context);
    gcmSetWaitFlip(gcm_context);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("video-test: alive\n");

    if (init_video() != 0) {
        fflush(stdout);
        return 1;
    }

    /* Host command buffer: 1 MB alignment is the mapping granularity into RSX space. */
    u32 host_size = 8 * 1024 * 1024;
    void *host_address = memalign(1024 * 1024, host_size);
    if (host_address == NULL) {
        printf("video-test: FAIL host alloc\n");
        return 1;
    }

    if (rsxInit(&gcm_context, CB_SIZE, host_size, host_address) != 0) {
        printf("video-test: FAIL rsxInit\n");
        return 1;
    }
    if (rsx_ring_attach(gcm_context, (char *)host_address + 0x100000, 0x100000) != 0) {
        printf("video-test: FAIL ring attach\n");
        return 1;
    }
    printf("video-test: rsx context ok\n");

    color_pitch = display_width * 4;
    color_pitch = (color_pitch + 63) & ~63u;
    depth_pitch = display_width * 4;
    depth_pitch = (depth_pitch + 63) & ~63u;

    for (u32 i = 0; i < FRAME_COUNT; i++) {
        color_buffer[i] = rsxMemalign(64, color_pitch * display_height);
        if (color_buffer[i] == NULL) {
            printf("video-test: FAIL colour buffer %u\n", i);
            return 1;
        }

        if (rsxAddressToOffset(color_buffer[i], &color_offset[i]) != 0) {
            printf("video-test: FAIL rsxAddressToOffset %u\n", i);
            return 1;
        }

        if (gcmSetDisplayBuffer((u8)i, color_offset[i], color_pitch,
                                display_width, display_height) != 0) {
            printf("video-test: FAIL gcmSetDisplayBuffer %u\n", i);
            return 1;
        }

        printf("video-test: fb%u offset 0x%08x pitch %u\n", i, color_offset[i], color_pitch);
    }

    depth_buffer = rsxMemalign(64, depth_pitch * display_height);
    if (depth_buffer == NULL) {
        printf("video-test: FAIL depth buffer\n");
        return 1;
    }
    rsxAddressToOffset(depth_buffer, &depth_offset);
    printf("video-test: depth offset 0x%08x pitch %u\n", depth_offset, depth_pitch);

    gcmSetFlipMode(GCM_FLIP_VSYNC);
    printf("video-test: rendering %d frames\n", TEST_FRAMES);
    fflush(stdout);

    for (u32 frame = 0; frame < TEST_FRAMES; frame++) {
        u32 index = frame % FRAME_COUNT;
        u32 fade = frame & 0xFF;
        u32 color = 0xFF000000u | (fade << 16) | ((0xFF - fade) << 8) | (fade << 0);

        rsx_ring_make_room(gcm_context);
        set_surface(index);

        rsxSetClearColor(gcm_context, color);
        rsxClearSurface(gcm_context, GCM_CLEAR_R | GCM_CLEAR_G | GCM_CLEAR_B | GCM_CLEAR_A);
        for (int fill = 0; fill < 1000; fill++) rsxSetClearColor(gcm_context, color);

        flip(index);

        if (frame % 60 == 0) {
            printf("video-test: frame %u\n", frame);
            fflush(stdout);
        }
    }

    printf("video-test: done\n");
    fflush(stdout);

    sleep(1);
    return 0;
}
