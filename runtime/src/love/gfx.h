#ifndef GFX_H
#define GFX_H

#include <stdbool.h>

#include <ppu-types.h>

#define GFX_MAX_SLOTS 12

typedef struct {
    f32 x, y;
    f32 u, v;
    f32 r, g, b, a;
} GfxVertex;

/* An image or a canvas. `width` and `height` are the logical size the game sees, which
 * differs from the pixel size when an image has a DPI scale. */
typedef struct {
    void *pixels;
    u32 offset;
    u32 pitch;
    u32 pixel_width;
    u32 pixel_height;
    f32 width;
    f32 height;
    bool nearest;
    bool render_target;
} GfxTexture;

typedef struct GfxProgram GfxProgram;

typedef struct GfxFont GfxFont;

typedef struct {
    f32 u0, v0, u1, v1;
    f32 width, height;
    f32 left, top;
    f32 advance;
} GfxGlyph;

/* Device: display, surfaces and flipping. */
int gfx_open(void);
void gfx_close(void);
u32 gfx_screen_width(void);
u32 gfx_screen_height(void);

/* Frame control. A NULL canvas means the screen. */
void gfx_set_canvas(GfxTexture *canvas);
void gfx_clear(f32 r, f32 g, f32 b, f32 a);
void gfx_present(void);
int gfx_debug_dump_screen(const char *path);

/* Returns room for vertex_count vertices drawn as triangles with this texture and the
 * current program. A NULL texture means plain colour. */
GfxVertex *gfx_reserve(GfxTexture *texture, u32 vertex_count);
void gfx_hold_batch(bool held);
void gfx_texture_changed(void);
u32 gfx_draw_calls_last_frame(void);

/* Textures. */
GfxTexture *gfx_texture_load_cooked(const char *path, f32 dpi_scale);
GfxTexture *gfx_texture_create(u32 width, u32 height, bool render_target);
void gfx_texture_bind(GfxTexture *texture);

/* Programs. */
int gfx_programs_open(void);
GfxProgram *gfx_program_find(const char *name);
GfxProgram *gfx_program_default(void);
void gfx_program_set_uniform(GfxProgram *program, const char *name, const f32 *values, int count);
void gfx_set_program(GfxProgram *program);
GfxProgram *gfx_get_program(void);
bool gfx_program_is_dirty(const GfxProgram *program);
void gfx_program_bind(GfxProgram *program, u32 target_width, u32 target_height);

/* Fonts. */
GfxFont *gfx_font_open(const char *path, u32 pixel_size);
const GfxGlyph *gfx_font_glyph(GfxFont *font, u32 codepoint);
GfxTexture *gfx_font_texture(GfxFont *font);
f32 gfx_font_height(const GfxFont *font);
f32 gfx_font_width(GfxFont *font, const char *text);
u32 gfx_utf8_next(const char **cursor);

#endif
