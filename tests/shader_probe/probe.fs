// Integration probe: outputs known uniforms as colours, so a screen dump proves that each
// component reaches the fragment program through the interpolators.
extern highp vec4 a;
extern highp vec2 b;
extern highp number c;
extern highp number d;

vec4 effect( vec4 colour, Image texture, vec2 texture_coords, vec2 screen_coords )
{
    if (screen_coords.y > 499.) return vec4(0., 1., 0., 0.5);
    // Shaped like splash.fs: uv stays live in .xy, so atan's |...| sources get a .z destination.
    if (screen_coords.x > 1024. && screen_coords.y < 100.) {
        vec2 uv = floor(b*10. + 0.25)/10.;
        float angle = atan(uv.y, uv.x);
        float len = length(uv);
        return vec4(angle/1.6, len, 0., 1.);
    }
    if (screen_coords.x < 256.) return vec4(a.r, a.g, a.b, 1.);
    if (screen_coords.x < 512.) return vec4(a.a, b.x, b.y, 1.);
    if (screen_coords.x < 768.) return vec4(c, d, 0., 1.);
    if (screen_coords.x < 1024.) return vec4(colour.r, colour.g, colour.b, 1.);
    return vec4(screen_coords.x/1280., screen_coords.y/720., love_ScreenSize.x/2560., 1.);
}
