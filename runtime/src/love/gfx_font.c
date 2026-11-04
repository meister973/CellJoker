/* TrueType fonts through FreeType, with glyphs rasterised on first use into one atlas. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "gfx.h"

#define ATLAS_SIZE       2048
#define GLYPH_PADDING    2
#define GLYPH_SLOTS      1024
#define GLYPH_SLOTS_FULL (GLYPH_SLOTS * 3 / 4)

/* CJK faces have 10000+ glyphs. They rasterise smaller, into a smaller atlas, so a screen of
 * text fits and all four CJK fonts fit in RSX memory. */
#define LARGE_FACE_GLYPHS  10000
#define LARGE_FACE_PIXELS  64
#define LARGE_ATLAS_HEIGHT 1024

typedef struct {
    u32 codepoint;
    bool used;
    GfxGlyph glyph;
} GlyphSlot;

struct GfxFont {
    FT_Face face;
    GfxTexture *atlas;
    u32 atlas_width;
    u32 atlas_height;
    u32 pen_x;
    u32 pen_y;
    u32 row_height;
    f32 height;
    f32 ascent;
    f32 scale;
    u32 slots_used;
    GlyphSlot slots[GLYPH_SLOTS];
};

static FT_Library library;

GfxFont *gfx_font_open(const char *path, u32 pixel_size)
{
    if (library == NULL && FT_Init_FreeType(&library) != 0)
        return NULL;

    GfxFont *font = calloc(1, sizeof(GfxFont));

    if (font == NULL)
        return NULL;
    /* Read from disk on demand: the CJK faces total 63 MB, too much for main RAM. */
    if (FT_New_Face(library, path, 0, &font->face) != 0 || FT_Set_Pixel_Sizes(font->face, 0, pixel_size) != 0) {
        free(font);
        return NULL;
    }

    font->pen_x = GLYPH_PADDING;
    font->pen_y = GLYPH_PADDING;
    font->ascent = font->face->size->metrics.ascender >> 6;
    font->height = font->face->size->metrics.height >> 6;
    font->scale = 1.0f;
    font->atlas_width = ATLAS_SIZE;
    font->atlas_height = ATLAS_SIZE;

    if (font->face->num_glyphs > LARGE_FACE_GLYPHS) {
        font->atlas_height = LARGE_ATLAS_HEIGHT;
        if (pixel_size > LARGE_FACE_PIXELS) {
            if (FT_Set_Pixel_Sizes(font->face, 0, LARGE_FACE_PIXELS) != 0)
                return NULL;
            font->scale = (f32)pixel_size / LARGE_FACE_PIXELS;
        }
    }
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
    if (font->pen_x + width + GLYPH_PADDING > font->atlas_width) {
        font->pen_x = GLYPH_PADDING;
        font->pen_y += font->row_height + GLYPH_PADDING;
        font->row_height = 0;
    }
    if (font->pen_y + height + GLYPH_PADDING > font->atlas_height)
        return -1;
    if (height > font->row_height)
        font->row_height = height;
    return 0;
}

/* Empties the atlas and the glyph table. Draws already queued may still sample the old glyphs. */
static void reset_atlas(GfxFont *font)
{
    printf("font: glyph cache full, starting over\n");
    gfx_wait_idle();
    gfx_texture_changed();
    if (font->atlas != NULL)
        memset(font->atlas->pixels, 0, font->atlas->pitch * font->atlas_height);
    memset(font->slots, 0, sizeof(font->slots));
    font->slots_used = 0;
    font->pen_x = GLYPH_PADDING;
    font->pen_y = GLYPH_PADDING;
    font->row_height = 0;
}

/* Returns -1 when the atlas has no room left for this glyph. */
static int rasterise(GfxFont *font, u32 codepoint, GfxGlyph *glyph)
{
    memset(glyph, 0, sizeof(*glyph));

    if (FT_Load_Char(font->face, codepoint, FT_LOAD_RENDER) != 0)
        return 0;

    FT_GlyphSlot slot = font->face->glyph;
    const FT_Bitmap *bitmap = &slot->bitmap;

    glyph->advance = (f32)(slot->advance.x >> 6) * font->scale;
    if (bitmap->width == 0 || bitmap->rows == 0)
        return 0;

    /* Made on first use: a font the game opens but never draws costs no RSX memory. */
    if (font->atlas == NULL) {
        font->atlas = gfx_texture_create(font->atlas_width, font->atlas_height, false);
        if (font->atlas == NULL) {
            printf("font: no RSX memory for a %ux%u atlas\n", font->atlas_width, font->atlas_height);
            return 0;
        }
        memset(font->atlas->pixels, 0, font->atlas->pitch * font->atlas_height);
    }

    if (place_in_atlas(font, bitmap->width, bitmap->rows) != 0)
        return -1;

    /* The batch may hold draws that sample the atlas, and the texture cache must refresh. */
    gfx_texture_changed();
    copy_bitmap(font, bitmap);

    glyph->width = (f32)bitmap->width * font->scale;
    glyph->height = (f32)bitmap->rows * font->scale;
    glyph->left = (f32)slot->bitmap_left * font->scale;
    glyph->top = font->ascent - (f32)slot->bitmap_top * font->scale;
    glyph->u0 = (f32)font->pen_x / font->atlas_width;
    glyph->v0 = (f32)font->pen_y / font->atlas_height;
    glyph->u1 = (f32)(font->pen_x + bitmap->width) / font->atlas_width;
    glyph->v1 = (f32)(font->pen_y + bitmap->rows) / font->atlas_height;

    font->pen_x += bitmap->width + GLYPH_PADDING;
    return 0;
}

/* The table is never full, so the probe always ends. */
static GlyphSlot *find_slot(GfxFont *font, u32 codepoint)
{
    u32 index = codepoint % GLYPH_SLOTS;

    while (font->slots[index].used && font->slots[index].codepoint != codepoint)
        index = (index + 1) % GLYPH_SLOTS;
    return &font->slots[index];
}

const GfxGlyph *gfx_font_glyph(GfxFont *font, u32 codepoint)
{
    GlyphSlot *slot = find_slot(font, codepoint);

    if (slot->used)
        return &slot->glyph;

    /* CJK text can need more glyphs than one atlas holds, so a full cache starts over. */
    if (font->slots_used == GLYPH_SLOTS_FULL) {
        reset_atlas(font);
        slot = find_slot(font, codepoint);
    }
    if (rasterise(font, codepoint, &slot->glyph) != 0) {
        reset_atlas(font);
        slot = find_slot(font, codepoint);
        rasterise(font, codepoint, &slot->glyph);
    }

    slot->used = true;
    slot->codepoint = codepoint;
    font->slots_used++;
    return &slot->glyph;
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
