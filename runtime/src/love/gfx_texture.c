/* RSX textures: cooked images from disk (tools/texture_cook.py), and render targets. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <rsx/rsx.h>
#include <rsx/gcm_sys.h>

#include "gfx.h"
#include "gfx_internal.h"

#define RTEX_HEADER_BYTES 22
#define RTEX_LEVEL_BYTES  8
#define COPY_CHUNK_BYTES  (1024 * 1024)
#define TEXTURE_ALIGNMENT 128

static u32 read_be16(const u8 *bytes) { return ((u32)bytes[0] << 8) | bytes[1]; }

static u32 read_be32(const u8 *bytes)
{
    return ((u32)bytes[0] << 24) | ((u32)bytes[1] << 16) | ((u32)bytes[2] << 8) | bytes[3];
}

static GfxTexture *new_texture(u32 pixel_width, u32 pixel_height, u32 pitch)
{
    GfxTexture *texture = calloc(1, sizeof(GfxTexture));

    if (texture == NULL)
        return NULL;

    texture->pixels = rsxMemalign(TEXTURE_ALIGNMENT, pitch * pixel_height);
    if (texture->pixels == NULL || rsxAddressToOffset(texture->pixels, &texture->offset) != 0) {
        free(texture);
        return NULL;
    }

    texture->pitch = pitch;
    texture->pixel_width = pixel_width;
    texture->pixel_height = pixel_height;
    texture->width = (f32)pixel_width;
    texture->height = (f32)pixel_height;
    return texture;
}

/* Reads go through main memory: LV2 file reads into RSX memory are not dependable on
 * hardware, and CPU writes to GDDR3 are fine. */
static int copy_file_to_rsx(FILE *file, void *destination, u32 size)
{
    u8 *chunk = malloc(COPY_CHUNK_BYTES);
    u8 *target = destination;

    if (chunk == NULL)
        return -1;

    while (size > 0) {
        u32 part = size < COPY_CHUNK_BYTES ? size : COPY_CHUNK_BYTES;

        if (fread(chunk, 1, part, file) != part) {
            free(chunk);
            return -1;
        }
        memcpy(target, chunk, part);
        target += part;
        size -= part;
    }

    free(chunk);
    return 0;
}

GfxTexture *gfx_texture_load_cooked(const char *path, f32 dpi_scale)
{
    u8 header[RTEX_HEADER_BYTES + RTEX_LEVEL_BYTES];
    FILE *file = fopen(path, "rb");

    if (file == NULL)
        return NULL;

    if (fread(header, 1, sizeof(header), file) != sizeof(header) || memcmp(header, "RTEX", 4) != 0) {
        fclose(file);
        return NULL;
    }

    u32 width = read_be16(header + 12);
    u32 height = read_be16(header + 14);
    u32 data_offset = read_be32(header + RTEX_HEADER_BYTES);
    u32 data_size = read_be32(header + RTEX_HEADER_BYTES + 4);
    GfxTexture *texture = new_texture(width, height, width * 4);

    if (texture == NULL || fseek(file, data_offset, SEEK_SET) != 0 ||
        copy_file_to_rsx(file, texture->pixels, data_size) != 0) {
        fclose(file);
        return NULL;
    }

    fclose(file);
    texture->width = width / dpi_scale;
    texture->height = height / dpi_scale;
    return texture;
}

GfxTexture *gfx_texture_create(u32 width, u32 height, bool render_target)
{
    GfxTexture *texture = new_texture(width, height, (width * 4 + 63) & ~63u);

    if (texture != NULL)
        texture->render_target = render_target;
    return texture;
}

void gfx_texture_bind(GfxTexture *texture)
{
    gcmTexture description;
    u8 filter = texture->nearest ? GCM_TEXTURE_NEAREST : GCM_TEXTURE_LINEAR;

    rsxInvalidateTextureCache(gfx_context, GCM_INVALIDATE_TEXTURE);

    description.format = GCM_TEXTURE_FORMAT_A8R8G8B8 | GCM_TEXTURE_FORMAT_LIN;
    description.mipmap = 1;
    description.dimension = GCM_TEXTURE_DIMS_2D;
    description.cubemap = GCM_FALSE;
    description.remap = (GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_B_SHIFT) |
                        (GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_G_SHIFT) |
                        (GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_R_SHIFT) |
                        (GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_A_SHIFT) |
                        (GCM_TEXTURE_REMAP_COLOR_B << GCM_TEXTURE_REMAP_COLOR_B_SHIFT) |
                        (GCM_TEXTURE_REMAP_COLOR_G << GCM_TEXTURE_REMAP_COLOR_G_SHIFT) |
                        (GCM_TEXTURE_REMAP_COLOR_R << GCM_TEXTURE_REMAP_COLOR_R_SHIFT) |
                        (GCM_TEXTURE_REMAP_COLOR_A << GCM_TEXTURE_REMAP_COLOR_A_SHIFT);
    description.width = texture->pixel_width;
    description.height = texture->pixel_height;
    description.depth = 1;
    description.location = GCM_LOCATION_RSX;
    description.pitch = texture->pitch;
    description.offset = texture->offset;

    rsxLoadTexture(gfx_context, 0, &description);
    rsxTextureControl(gfx_context, 0, GCM_TRUE, 0 << 8, 12 << 8, GCM_TEXTURE_MAX_ANISO_1);
    rsxTextureFilter(gfx_context, 0, 0, filter, filter, GCM_TEXTURE_CONVOLUTION_QUINCUNX);
    rsxTextureWrapMode(gfx_context, 0, GCM_TEXTURE_CLAMP_TO_EDGE, GCM_TEXTURE_CLAMP_TO_EDGE,
                       GCM_TEXTURE_CLAMP_TO_EDGE, 0, GCM_TEXTURE_ZFUNC_LESS, 0);
}
