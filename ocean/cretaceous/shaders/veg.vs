#version 330
#include "depth.glsl"
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_normal;   // xyz normal (bent for foliage), w crown occlusion
layout(location = 3) in vec4 a_attr;     // layer, wind weight (0..255), leaf flag
layout(location = 4) in vec4 i_ps;       // position, scale
layout(location = 5) in vec4 i_rot;      // cos yaw, sin yaw, hue, phase
layout(location = 6) in vec4 i_extra;    // fade
uniform mat4 u_viewproj;
uniform float u_time;
uniform int u_bake;
out vec3 v_world, v_normal, v_base;
out vec2 v_uv;
out float v_ao, v_leaf, v_hue, v_fade, v_w, v_var;
flat out float v_bark;
flat out float v_layer;
void main() {
    float c = i_rot.x, s = i_rot.y, sc = i_ps.w;
    vec3 lp = a_pos * sc;
    float wind = a_attr.y / 255.0;
    if (u_bake == 0) {
        float t = u_time, ph = i_rot.w;
        vec2 wd = vec2(0.8, 0.6);
        float gust = 0.55 + 0.45 * sin(t * 0.31 + i_ps.x * 0.011 + i_ps.z * 0.017);
        float sway = sin(t * 1.25 + ph) * 0.6 + sin(t * 2.6 + ph * 1.7) * 0.25;
        lp.xz += wd * (sway * gust * wind * (0.12 + 0.012 * lp.y));
        lp += a_normal.xyz * (sin(t * 5.5 + dot(a_pos, vec3(3.1, 1.7, 2.3)) + ph) * 0.035 * wind * a_attr.z);
    }
    vec3 wp = vec3(c * lp.x - s * lp.z, lp.y, s * lp.x + c * lp.z) + i_ps.xyz;
    vec3 n = a_normal.xyz;
    v_normal = vec3(c * n.x - s * n.z, n.y, s * n.x + c * n.z);
    v_world = wp; v_base = i_ps.xyz; v_uv = a_uv; v_layer = a_attr.x;
    v_ao = a_normal.w; v_leaf = a_attr.z; v_bark = a_attr.w; v_hue = i_rot.z; v_fade = i_extra.x;
    // Per-leaf variation: a hash of the mesh-space cell (about 40 cm), so
    // neighbouring leaves and fronds differ a little and no two plants match.
    vec3 hc = floor(a_pos * 2.5) + i_rot.w * 7.0;
    v_var = fract(sin(dot(hc, vec3(12.99, 78.23, 37.72))) * 43758.55);
    vec4 clip = u_viewproj * vec4(wp, 1.0);
    v_w = clip.w; gl_Position = scene_clip(clip);
}
