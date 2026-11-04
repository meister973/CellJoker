/*
 * RSX device and batching: display setup, surfaces, flips, and one vertex ring. Vertices
 * are already in target pixels, and consecutive draws with the same state share a call.
 */
#include <malloc.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <rsx/rsx.h>
#include <rsx/gcm_sys.h>
#include <sysutil/video.h>

#include "gfx.h"
#include "gfx_internal.h"
#include "rsx_ring.h"

#define HOST_SIZE          (8 * 1024 * 1024)
#define INITIAL_CB_SIZE    0x80000
#define RING_OFFSET        0x100000
#define RING_SIZE          (4 * 1024 * 1024)
#define BACK_BUFFER_COUNT  2
#define VERTEX_BYTES_EACH  (4 * 1024 * 1024)
#define VERTEX_CAPACITY    (VERTEX_BYTES_EACH / sizeof(GfxVertex))
#define LABEL_INDEX        255

gcmContextData *gfx_context;

static u32 screen_width;
static u32 screen_height;
static u32 screen_pitch;
static u32 back_offset[BACK_BUFFER_COUNT];
static void *back_pixels[BACK_BUFFER_COUNT];
static u32 depth_offset;
static u32 depth_pitch;
static u32 back_index;
static bool first_flip = true;

static GfxVertex *vertex_memory;
static u32 vertex_offset_base;
static u32 frame_half;
static u32 vertex_cursor;
static u32 pending_first;
static u32 pending_count;

static GfxTexture *current_canvas;
static GfxTexture *bound_texture;
static GfxTexture *white_texture;
static GfxProgram *bound_program;
static bool texture_stale = true;
static bool batch_held;
static u32 draw_calls;
static u32 draw_calls_last_frame;
static u32 label_value = 1;

static int configure_display(void)
{
    videoState state;
    videoResolution resolution;
    videoConfiguration config;

    if (videoGetState(0, 0, &state) != 0)
        return -1;
    /* 720p when the display offers it: fill rate is the risk at 1080p on an RSX. */
    u8 mode = state.displayMode.resolution;
    if (videoGetResolutionAvailability(0, VIDEO_RESOLUTION_720, VIDEO_ASPECT_AUTO, 0) == 1)
        mode = VIDEO_RESOLUTION_720;

    if (videoGetResolution(mode, &resolution) != 0)
        return -1;

    screen_width = resolution.width;
    screen_height = resolution.height;
    screen_pitch = (screen_width * 4 + 63) & ~63u;

    memset(&config, 0, sizeof(config));
    config.resolution = mode;
    config.format = VIDEO_BUFFER_FORMAT_XRGB;
    config.aspect = state.displayMode.aspect;
    config.pitch = screen_pitch;
    return videoConfigure(0, &config, NULL, 0);
}

static int allocate_surfaces(void)
{
    for (u32 i = 0; i < BACK_BUFFER_COUNT; i++) {
        void *memory = rsxMemalign(64, screen_pitch * screen_height);

        if (memory == NULL || rsxAddressToOffset(memory, &back_offset[i]) != 0)
            return -1;
        back_pixels[i] = memory;
        if (gcmSetDisplayBuffer((u8)i, back_offset[i], screen_pitch, screen_width, screen_height) != 0)
            return -1;
    }

    depth_pitch = screen_pitch;
    void *depth = rsxMemalign(64, depth_pitch * screen_height);
    if (depth == NULL)
        return -1;
    return rsxAddressToOffset(depth, &depth_offset);
}

static int allocate_vertices(void)
{
    vertex_memory = rsxMemalign(128, VERTEX_BYTES_EACH * 2);
    if (vertex_memory == NULL)
        return -1;
    return rsxAddressToOffset(vertex_memory, &vertex_offset_base);
}

/* The blend matches LÖVE's default "alpha" mode: straight alpha for colour, and a
 * target alpha that accumulates. */
static void set_fixed_state(void)
{
    rsxSetColorMask(gfx_context, GCM_COLOR_MASK_B | GCM_COLOR_MASK_G | GCM_COLOR_MASK_R | GCM_COLOR_MASK_A);
    rsxSetColorMaskMrt(gfx_context, 0);
    rsxSetDepthTestEnable(gfx_context, GCM_FALSE);
    rsxSetDepthWriteEnable(gfx_context, GCM_FALSE);
    rsxSetCullFaceEnable(gfx_context, GCM_FALSE);
    rsxSetShadeModel(gfx_context, GCM_SHADE_MODEL_SMOOTH);
    rsxSetFrontFace(gfx_context, GCM_FRONTFACE_CCW);
    rsxSetZMinMaxControl(gfx_context, GCM_FALSE, GCM_TRUE, GCM_FALSE);
    rsxSetUserClipPlaneControl(gfx_context,
                               GCM_USER_CLIP_PLANE_DISABLE, GCM_USER_CLIP_PLANE_DISABLE,
                               GCM_USER_CLIP_PLANE_DISABLE, GCM_USER_CLIP_PLANE_DISABLE,
                               GCM_USER_CLIP_PLANE_DISABLE, GCM_USER_CLIP_PLANE_DISABLE);
    rsxSetBlendEnable(gfx_context, GCM_TRUE);
    rsxSetBlendFunc(gfx_context, GCM_SRC_ALPHA, GCM_ONE_MINUS_SRC_ALPHA, GCM_ONE, GCM_ONE_MINUS_SRC_ALPHA);
    rsxSetBlendEquation(gfx_context, GCM_FUNC_ADD, GCM_FUNC_ADD);
}

static void bind_surface(u8 color_format, u32 offset, u32 pitch, u32 width, u32 height)
{
    gcmSurface surface;
    f32 scale[4] = { width * 0.5f, height * -0.5f, 0.5f, 0.0f };
    f32 bias[4] = { width * 0.5f, height * 0.5f, 0.5f, 0.0f };

    memset(&surface, 0, sizeof(surface));
    surface.type = GCM_SURFACE_TYPE_LINEAR;
    surface.antiAlias = GCM_SURFACE_CENTER_1;
    surface.colorFormat = color_format;
    surface.colorTarget = GCM_SURFACE_TARGET_0;
    surface.colorLocation[0] = GCM_LOCATION_RSX;
    surface.colorOffset[0] = offset;
    surface.colorPitch[0] = pitch;
    for (u32 i = 1; i < GCM_MAX_MRT_COUNT; i++) {
        surface.colorLocation[i] = GCM_LOCATION_RSX;
        surface.colorPitch[i] = 64;
    }
    surface.depthFormat = GCM_SURFACE_ZETA_Z24S8;
    surface.depthLocation = GCM_LOCATION_RSX;
    surface.depthOffset = depth_offset;
    surface.depthPitch = depth_pitch;
    surface.width = width;
    surface.height = height;

    rsxSetSurface(gfx_context, &surface);
    set_fixed_state();
    rsxSetViewport(gfx_context, 0, 0, width, height, 0.0f, 1.0f, scale, bias);
    rsxSetScissor(gfx_context, 0, 0, width, height);
    for (u32 i = 0; i < 8; i++)
        rsxSetViewportClip(gfx_context, i, width, height);
}

static void bind_vertex_arrays(void)
{
    u32 base = vertex_offset_base + frame_half * VERTEX_BYTES_EACH;

    rsxBindVertexArrayAttrib(gfx_context, GCM_VERTEX_ATTRIB_POS, 0, base + offsetof(GfxVertex, x),
                             sizeof(GfxVertex), 2, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
    rsxBindVertexArrayAttrib(gfx_context, GCM_VERTEX_ATTRIB_TEX0, 0, base + offsetof(GfxVertex, u),
                             sizeof(GfxVertex), 2, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
    rsxBindVertexArrayAttrib(gfx_context, GCM_VERTEX_ATTRIB_COLOR0, 0, base + offsetof(GfxVertex, r),
                             sizeof(GfxVertex), 4, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
}

static void bind_current_target(void)
{
    if (current_canvas == NULL) {
        bind_surface(GCM_SURFACE_X8R8G8B8, back_offset[back_index], screen_pitch, screen_width, screen_height);
        return;
    }
    bind_surface(GCM_SURFACE_A8R8G8B8, current_canvas->offset, current_canvas->pitch,
                 current_canvas->pixel_width, current_canvas->pixel_height);
}

static void flush_batch(void)
{
    if (pending_count == 0)
        return;

    rsxInvalidateVertexCache(gfx_context);
    rsxDrawVertexArray(gfx_context, GCM_TYPE_TRIANGLES, pending_first, pending_count);
    draw_calls++;
    pending_first += pending_count;
    pending_count = 0;
}

/* The ring overflowed mid-frame: let the RSX catch up, then reuse this half. */
static void wait_for_rsx(void)
{
    rsxSetWriteBackendLabel(gfx_context, LABEL_INDEX, label_value);
    rsxFlushBuffer(gfx_context);
    while (*(vu32 *)gcmGetLabelAddress(LABEL_INDEX) != label_value)
        usleep(30);
    label_value++;
}

int gfx_open(void)
{
    void *host = memalign(1024 * 1024, HOST_SIZE);

    if (host == NULL || rsxInit(&gfx_context, INITIAL_CB_SIZE, HOST_SIZE, host) != 0)
        return -1;
    if (rsx_ring_attach(gfx_context, (char *)host + RING_OFFSET, RING_SIZE) != 0)
        return -1;
    if (configure_display() != 0 || allocate_surfaces() != 0 || allocate_vertices() != 0)
        return -1;

    gcmSetFlipMode(GCM_FLIP_VSYNC);
    set_fixed_state();
    bind_vertex_arrays();
    bind_current_target();

    white_texture = gfx_texture_create(4, 4, false);
    if (white_texture == NULL)
        return -1;
    memset(white_texture->pixels, 0xFF, white_texture->pitch * white_texture->pixel_height);
    return gfx_programs_open();
}

/* Without this, the RSX keeps reading freed memory after the process leaves. */
void gfx_close(void)
{
    if (gfx_context == NULL)
        return;
    gcmSetWaitFlip(gfx_context);
    wait_for_rsx();
}

u32 gfx_screen_width(void) { return screen_width; }
u32 gfx_screen_height(void) { return screen_height; }
u32 gfx_draw_calls_last_frame(void) { return draw_calls_last_frame; }

void gfx_set_canvas(GfxTexture *canvas)
{
    if (canvas == current_canvas)
        return;

    rsx_ring_make_room(gfx_context);
    flush_batch();
    current_canvas = canvas;
    bind_current_target();
    /* A canvas just rendered may be sampled next, and the projection follows the target. */
    bound_program = NULL;
    texture_stale = true;
}

void gfx_clear(f32 r, f32 g, f32 b, f32 a)
{
    u32 color = ((u32)(a * 255.0f + 0.5f) << 24) | ((u32)(r * 255.0f + 0.5f) << 16) |
                ((u32)(g * 255.0f + 0.5f) << 8) | (u32)(b * 255.0f + 0.5f);

    rsx_ring_make_room(gfx_context);
    flush_batch();
    rsxSetClearColor(gfx_context, color);
    rsxClearSurface(gfx_context, GCM_CLEAR_R | GCM_CLEAR_G | GCM_CLEAR_B | GCM_CLEAR_A);
}

void gfx_hold_batch(bool held)
{
    batch_held = held;
}

void gfx_texture_changed(void)
{
    rsx_ring_make_room(gfx_context);
    flush_batch();
    texture_stale = true;
}

/* Lets the CPU overwrite memory that queued draws still read. */
void gfx_wait_idle(void)
{
    rsx_ring_make_room(gfx_context);
    flush_batch();
    wait_for_rsx();
}

GfxVertex *gfx_reserve(GfxTexture *texture, u32 vertex_count)
{
    GfxTexture *wanted = texture != NULL ? texture : white_texture;
    GfxProgram *program = gfx_get_program();

    rsx_ring_make_room(gfx_context);

    /* Shapes that overlap inside one draw call do not blend over each other under RPCS3
     * (tests/shader_probe), so only glyphs of one string, which do not overlap, share one. */
    if (!batch_held)
        flush_batch();

    if (vertex_cursor + vertex_count > VERTEX_CAPACITY) {
        flush_batch();
        wait_for_rsx();
        vertex_cursor = 0;
        pending_first = 0;
    }

    if (wanted != bound_texture || texture_stale) {
        flush_batch();
        gfx_texture_bind(wanted);
        bound_texture = wanted;
        texture_stale = false;
    }

    if (program != bound_program || gfx_program_is_dirty(program)) {
        flush_batch();
        u32 width = current_canvas != NULL ? current_canvas->pixel_width : screen_width;
        u32 height = current_canvas != NULL ? current_canvas->pixel_height : screen_height;

        gfx_program_bind(program, width, height);
        bound_program = program;
    }

    GfxVertex *vertices = vertex_memory + frame_half * VERTEX_CAPACITY + vertex_cursor;

    vertex_cursor += vertex_count;
    pending_count += vertex_count;
    return vertices;
}

/* Test tooling only: the CPU reads GDDR3 at about 16 MB/s on hardware, and RPCS3 needs
 * "Write Color Buffers" on to keep this memory current. Writes a binary PPM. */
int gfx_debug_dump_screen(const char *path)
{
    flush_batch();
    wait_for_rsx();

    FILE *file = fopen(path, "wb");
    if (file == NULL)
        return -1;

    u8 *row = malloc(screen_width * 3);
    if (row == NULL) {
        fclose(file);
        return -1;
    }

    fprintf(file, "P6\n%u %u\n255\n", screen_width, screen_height);
    for (u32 y = 0; y < screen_height; y++) {
        const u8 *source = (const u8 *)back_pixels[back_index] + y * screen_pitch;

        for (u32 x = 0; x < screen_width; x++) {
            row[x * 3] = source[x * 4 + 1];
            row[x * 3 + 1] = source[x * 4 + 2];
            row[x * 3 + 2] = source[x * 4 + 3];
        }
        fwrite(row, 1, screen_width * 3, file);
    }

    free(row);
    fclose(file);
    return 0;
}

static void wait_previous_flip(void)
{
    if (first_flip) {
        gcmResetFlipStatus();
        first_flip = false;
        return;
    }
    while (gcmGetFlipStatus() != 0)
        usleep(200);
    gcmResetFlipStatus();
}

void gfx_present(void)
{
    rsx_ring_make_room(gfx_context);
    flush_batch();

    if (current_canvas != NULL) {
        current_canvas = NULL;
        bind_current_target();
    }

    wait_previous_flip();
    gcmSetFlip(gfx_context, (u8)back_index);
    rsxFlushBuffer(gfx_context);
    gcmSetWaitFlip(gfx_context);

    back_index ^= 1;
    bind_current_target();

    /* The other half was last drawn two frames ago, and that flip has finished. */
    frame_half ^= 1;
    vertex_cursor = 0;
    pending_first = 0;
    bind_vertex_arrays();

    bound_program = NULL;
    texture_stale = true;
    draw_calls_last_frame = draw_calls;
    draw_calls = 0;
}
