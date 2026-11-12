#version 330
#include "depth.glsl"
layout(location = 0) in vec2 a_corner;   // x -1..1, y 0..1
layout(location = 4) in vec4 i_ps;
layout(location = 5) in vec4 i_rot;
layout(location = 6) in vec4 i_extra;    // mesh-range factor, first layer, radius, height
uniform mat4 u_viewproj;
uniform vec3 u_cam;
uniform vec3 u_imp_range;                // mesh range, crossfade band, far range (veg.c imp_uniforms)
uniform int u_views;
out vec3 v_world, v_base;
out vec2 v_uv;
out float v_fade, v_w, v_hue, v_blend;
flat out float v_layer0, v_layer1;
flat out vec2 v_rot;
void main() {
    vec3 base = i_ps.xyz; float sc = i_ps.w, R = i_extra.z * sc, H = i_extra.w * sc;
    // Every tree is in the static buffer (veg.c build_impostors): it fades in
    // over the band before its mesh range ends and out over the last 15% of
    // the far range. Outside both it collapses to a point off screen.
    float d = distance(base, u_cam), n = u_imp_range.x * i_extra.x;
    float fade = smoothstep(n - u_imp_range.y, n, d) * (1.0 - smoothstep(u_imp_range.z * 0.85, u_imp_range.z, d));
    if (fade <= 0.0) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); return; }
    vec2 th = normalize(u_cam.xz - base.xz + 1e-4);            // toward the camera
    vec3 right = vec3(th.y, 0.0, -th.x);
    float c = i_rot.x, s = i_rot.y;
    vec2 ob = vec2(c * th.x + s * th.y, -s * th.x + c * th.y); // object space
    float az = atan(ob.y, ob.x); if (az < 0.0) az += 6.2831853;
    float f = az / 6.2831853 * float(u_views), k0 = floor(f);
    v_layer0 = i_extra.y + mod(k0, float(u_views)); v_layer1 = i_extra.y + mod(k0 + 1.0, float(u_views)); v_blend = f - k0;
    // Pull the card toward the camera by the crown radius so it sorts like the crown front.
    vec3 wp = base + right * (a_corner.x * R) + vec3(0.0, a_corner.y * H, 0.0) + vec3(th.x, 0.0, th.y) * R * 0.3;
    // The same fixed lean per plant as the mesh (veg.vs).
    float la = i_rot.w * 2.7, lean = (fract(i_rot.w * 1.618) - 0.5) * 0.09;
    wp.xz += vec2(cos(la), sin(la)) * (lean * a_corner.y * H);
    v_uv = vec2(a_corner.x * 0.5 + 0.5, a_corner.y);
    v_world = wp; v_base = base; v_fade = fade; v_hue = i_rot.z; v_rot = i_rot.xy;
    vec4 clip = u_viewproj * vec4(wp, 1.0);
    v_w = clip.w; gl_Position = scene_clip(clip);
}
