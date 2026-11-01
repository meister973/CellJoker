/* TrueType fonts through FreeType, with glyphs rasterised on first use into one atlas. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "gfx.h"

#define ATLAS_SIZE     2048
#define GLYPH_PADDING  2
#define GLYPH_SLOTS    1024

typedef struct {
    u32 codepoint;
    bool used;
    GfxGlyph glyph;
} GlyphSlot;

struct GfxFont {
    FT_Face face;
    GfxTexture *atlas;
    u32 pen_x;
    u32 pen_y;
    u32 row_height;
    f32 height;
    f32 ascent;
    GlyphSlot slots[GLYPH_SLOTS];
};

static FT_Library library;

static void *read_whole_file(const char *path, long *size)
{
    FILE *file = fopen(path, "rb");

    if (file == NULL)
        return NULL;

    fseek(file, 0, SEEK_END);
    *size = ftell(file);
    fseek(file, 0, SEEK_SET);

    void *bytes = malloc(*size);
    if (bytes != NULL && fread(bytes, 1, *size, file) != (size_t)*size) {
        free(bytes);
        bytes = NULL;
    }
    fclose(file);
    return bytes;
}

GfxFont *gfx_font_open(const char *path, u32 pixel_size)
{
    long size;

    if (library == NULL && FT_Init_FreeType(&library) != 0)
        return NULL;

    /* FreeType keeps reading from this memory for as long as the face lives. */
    void *bytes = read_whole_file(path, &size);
    GfxFont *font = calloc(1, sizeof(GfxFont));

    if (bytes == NULL || font == NULL)
        return NULL;
    if (FT_New_Memory_Face(library, bytes, size, 0, &font->face) != 0)
        return NULL;
    if (FT_Set_Pixel_Sizes(font->face, 0, pixel_size) != 0)
        return NULL;

    font->atlas = gfx_texture_create(ATLAS_SIZE, ATLAS_SIZE, false);
    if (font->atlas == NULL)
        return NULL;
    memset(font->atlas->pixels, 0, font->atlas->pitch * ATLAS_SIZE);

    font->pen_x = GLYPH_PADDING;
    font->pen_y = GLYPH_PADDING;
    font->ascent = font->face->size->metrics.ascender >> 6;
    font->height = font->face->size->metrics.height >> 6;
    return font;
}

/* White texels with coverage in alpha, as LÖVE's font images behave under colour * Texel. */
static void copy_bitmap(GfxFont *font, const FT_Bitmap *bitmap)
{
    u8 *destination_row = (u8 *)font->atlas->pixels + font->pen_y * font->atlas->pitch + font->pen_x * 4;
    const u8 *source_row = bitmap->buffer;

    for (u32 y = 0; y < bitmap->rows; y++) {
        u32 *destination = (u32 *)destination_row;

        for (u32 x = 0; x < bitmap->width; x++)
            destination[x] = ((u32)source_row[x] << 24) | 0x00FFFFFFu;
        destination_row += font->atlas->pitch;
        source_row += bitmap->pitch;
    }
}

static int place_in_atlas(GfxFont *font, u32 width, u32 height)
{
    if (font->pen_x + width + GLYPH_PADDING > ATLAS_SIZE) {
        font->pen_x = GLYPH_PADDING;
        font->pen_y += font->row_height + GLYPH_PADDING;
        font->row_height = 0;
    }
    if (font->pen_y + height + GLYPH_PADDING > ATLAS_SIZE)
        return -1;
    if (height > font->row_height)
        font->row_height = height;
    return 0;
}

static void rasterise(GfxFont *font, u32 codepoint, GfxGlyph *glyph)
{
    memset(glyph, 0, sizeof(*glyph));

    if (FT_Load_Char(font->face, codepoint, FT_LOAD_RENDER) != 0)
        return;

    FT_GlyphSlot slot = font->face->glyph;
    const FT_Bitmap *bitmap = &slot->bitmap;

    glyph->advance = (f32)(slot->advance.x >> 6);
    if (bitmap->width == 0 || bitmap->rows == 0)
        return;


    if (place_in_atlas(font, bitmap->width, bitmap->rows) != 0) {
        printf("font: atlas full at codepoint %u\n", codepoint);
        return;
    }

    /* The batch may hold draws that sample the atlas, and the texture cache must refresh. */
    gfx_texture_changed();
    copy_bitmap(font, bitmap);

    glyph->width = (f32)bitmap->width;
    glyph->height = (f32)bitmap->rows;
    glyph->left = (f32)slot->bitmap_left;
    glyph->top = font->ascent - (f32)slot->bitmap_top;
    glyph->u0 = (f32)font->pen_x / ATLAS_SIZE;
    glyph->v0 = (f32)font->pen_y / ATLAS_SIZE;
    glyph->u1 = (f32)(font->pen_x + bitmap->width) / ATLAS_SIZE;
    glyph->v1 = (f32)(font->pen_y + bitmap->rows) / ATLAS_SIZE;

    font->pen_x += bitmap->width + GLYPH_PADDING;
}

const GfxGlyph *gfx_font_glyph(GfxFont *font, u32 codepoint)
{
    u32 index = codepoint % GLYPH_SLOTS;

    for (u32 probe = 0; probe < GLYPH_SLOTS; probe++) {
        GlyphSlot *slot = &font->slots[(index + probe) % GLYPH_SLOTS];

        if (!slot->used) {
            slot->used = true;
            slot->codepoint = codepoint;
            rasterise(font, codepoint, &slot->glyph);
            return &slot->glyph;
        }
        if (slot->codepoint == codepoint)
            return &slot->glyph;
    }
    return &font->slots[index].glyph;
}

GfxTexture *gfx_font_texture(GfxFont *font) { return font->atlas; }
f32 gfx_font_height(const GfxFont *font) { return font->height; }

u32 gfx_utf8_next(const char **cursor)
{
    const u8 *bytes = (const u8 *)*cursor;
    u32 first = bytes[0];
    u32 extra = first >= 0xF0 ? 3 : first >= 0xE0 ? 2 : first >= 0xC0 ? 1 : 0;
    u32 codepoint = extra == 0 ? first : first & (0x3F >> extra);

    for (u32 i = 1; i <= extra; i++) {
        if ((bytes[i] & 0xC0) != 0x80) {
            *cursor += i;
            return codepoint;
        }
        codepoint = (codepoint << 6) | (bytes[i] & 0x3F);
    }

    *cursor += extra + 1;
    return codepoint;
}

f32 gfx_font_width(GfxFont *font, const char *text)
{
    f32 widest = 0.0f;
    f32 line = 0.0f;

    while (*text != '\0') {
        u32 codepoint = gfx_utf8_next(&text);

        if (codepoint == '\n') {
            line = 0.0f;
            continue;
        }
        line += gfx_font_glyph(font, codepoint)->advance;
        if (line > widest)
            widest = line;
    }
    return widest;
}
