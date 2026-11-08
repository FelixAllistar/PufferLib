#version 330
#include "depth.glsl"
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_normal;   // xyz normal (bent for foliage), w crown occlusion
layout(location = 3) in vec4 a_attr;     // layer, wind weight (0..255), leaf flag
layout(location = 4) in vec4 i_ps;       // position, scale
layout(location = 5) in vec4 i_rot;      // cos yaw, sin yaw, hue, phase
layout(location = 6) in vec4 i_extra;    // fade, -, bend (0 rigid .. 1 grass; ground cover only), plant height (m)
uniform mat4 u_viewproj;
uniform float u_time;
uniform int u_bake;
// Ground cover bending (render/veg.c, render/trample.h): u_wind = (downwind
// x, z, strength); t_trample holds the bend animals left, u_trample =
// (origin x, z, 1 / size, on).
uniform vec3 u_wind;
uniform sampler2D t_trample;
uniform vec4 u_trample;
out vec3 v_world, v_normal, v_base;
out vec2 v_uv;
out float v_ao, v_leaf, v_hue, v_fade, v_w, v_var;
flat out float v_bark;
flat out float v_layer;
void main() {
    float c = i_rot.x, s = i_rot.y, sc = i_ps.w;
    vec3 lp = a_pos * sc;
    float wind = a_attr.y / 255.0;
    vec3 n = a_normal.xyz;
    if (u_bake == 0) lp += a_normal.xyz * (sin(u_time * 5.5 + dot(a_pos, vec3(3.1, 1.7, 2.3)) + i_rot.w) * 0.035 * wind * a_attr.z);
    vec3 wl = vec3(c * lp.x - s * lp.z, lp.y, s * lp.x + c * lp.z);   // plant frame turned to the world
    n = vec3(c * n.x - s * n.z, n.y, s * n.x + c * n.z);
    if (u_bake == 0) {
        float t = u_time, ph = i_rot.w;
        vec2 wd = dot(u_wind.xy, u_wind.xy) > 0.0 ? u_wind.xy : vec2(0.8, 0.6);
        vec2 xz = wl.xz + i_ps.xz;
        // Gusts: broad bands rolling downwind at ~7 m/s, wavering across the
        // field, so neighbouring plants lean and sway together.
        float along = dot(xz, wd), across = dot(xz, vec2(-wd.y, wd.x));
        float g = 0.5 + 0.5 * sin(along * 0.09 - t * 0.63 + 1.6 * sin(across * 0.031 + t * 0.07));
        float gust = g * g * (0.55 + 0.45 * sin(t * 0.31 + i_ps.x * 0.011 + i_ps.z * 0.017));
        float sway = sin(t * 1.25 + ph + along * 0.15) * 0.6 + sin(t * 2.6 + ph * 1.7) * 0.25;
        wl.xz += wd * (sway * (0.3 + 0.9 * gust) * wind * (0.12 + 0.012 * lp.y));
        float bend = i_extra.z;
        if (bend > 0.0) {
            // Ground cover: lean with the gusts, and away from animals (trample map).
            vec2 B = wd * (u_wind.z * (0.06 + 0.32 * gust));
            if (u_trample.w > 0.0) {
                vec2 uv = (xz - u_trample.xy) * u_trample.z;
                float edge = smoothstep(0.0, 0.05, min(min(uv.x, uv.y), min(1.0 - uv.x, 1.0 - uv.y)));
                B += textureLod(t_trample, uv, 0.0).xy * edge;
            }
            float bl = length(B);
            if (bl > 1e-4) {
                // Each point swings about the ground below it, more toward the
                // tip: blades arc over rather than tilt, and never dip below ground.
                vec2 d = B / bl;
                float H = max(i_extra.w, 0.15), y = max(wl.y, 0.0);
                float th = min(bl, 1.15) * 1.3 * bend * (0.35 + 0.65 * clamp(y / H, 0.0, 1.0));
                float cs = cos(th), sn = sin(th);
                wl.xz += d * (y * sn);
                wl.y += max(y * cs, y * 0.12) - y;
                float na = dot(n.xz, d);
                n.xz += d * (na * cs + n.y * sn - na); n.y = n.y * cs - na * sn;
            }
        }
    }
    vec3 wp = wl + i_ps.xyz;
    v_normal = n;
    v_world = wp; v_base = i_ps.xyz; v_uv = a_uv; v_layer = a_attr.x;
    v_ao = a_normal.w; v_leaf = a_attr.z; v_bark = a_attr.w; v_hue = i_rot.z; v_fade = i_extra.x;
    // Per-leaf variation: a hash of the mesh-space cell (about 40 cm), so
    // neighbouring leaves and fronds differ a little and no two plants match.
    vec3 hc = floor(a_pos * 2.5) + i_rot.w * 7.0;
    v_var = fract(sin(dot(hc, vec3(12.99, 78.23, 37.72))) * 43758.55);
    vec4 clip = u_viewproj * vec4(wp, 1.0);
    v_w = clip.w; gl_Position = scene_clip(clip);
}
