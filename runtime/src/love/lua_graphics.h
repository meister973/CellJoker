#ifndef LUA_GRAPHICS_H
#define LUA_GRAPHICS_H

#include <lua.h>

#include "gfx.h"

typedef struct {
    f32 a, b, c, d, e, f;
} Affine;

int luaopen_love_graphics(lua_State *L);

/* Shared between the state functions and the drawable objects. */
const Affine *graphics_transform(void);
const f32 *graphics_color(void);
void affine_multiply(Affine *result, const Affine *left, const Affine *right);
void write_vertex(GfxVertex *vertex, const Affine *m, f32 x, f32 y, f32 u, f32 v, const f32 *color);
void read_color_table(lua_State *L, int index, f32 *color);

/* Adds the object constructors and love.graphics.draw to the table on top of the stack. */
void lua_graphics_register_objects(lua_State *L);

#endif
