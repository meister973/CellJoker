// Integration probe: each screen band shows one interpolator slot, because the game's
// larger shaders use more slots than probe.fs does.
extern highp vec4 s0;
extern highp vec4 s1;
extern highp vec4 s2;
extern highp vec4 s3;
extern highp vec4 s4;
extern highp vec4 s5;

vec4 effect( vec4 colour, Image texture, vec2 texture_coords, vec2 screen_coords )
{
    float band = floor(screen_coords.x/160.);
    float row = floor(screen_coords.y/360.);
    vec4 value = s0;
    if (band > 0.5) value = s1;
    if (band > 1.5) value = s2;
    if (band > 2.5) value = s3;
    if (band > 3.5) value = s4;
    if (band > 4.5) value = s5;
    return row < 0.5 ? vec4(value.x, value.y, value.z, 1.) : vec4(value.w, value.w, value.w, 1.);
}
