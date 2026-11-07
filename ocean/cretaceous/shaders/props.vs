#version 330
#include "depth.glsl"
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_normal;
layout(location = 3) in vec4 a_tangent;   // w = bitangent sign
layout(location = 4) in vec4 i_ps;        // position, scale
layout(location = 5) in vec4 i_q;         // rotation quaternion
layout(location = 6) in vec4 i_ex;        // fade, material layer, moss, tint
layout(location = 7) in vec4 i_misc;      // mesh height (unscaled)
uniform mat4 u_viewproj;
out vec3 v_world, v_n, v_t, v_base;
out vec2 v_uv;
out float v_w, v_fade, v_moss, v_tint, v_hfrac, v_bs;
flat out float v_layer;
vec3 qrot(vec4 q, vec3 v) { return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v); }
void main() {
    vec3 wp = qrot(i_q, a_pos * i_ps.w) + i_ps.xyz;
    v_world = wp; v_base = i_ps.xyz;
    v_n = qrot(i_q, a_normal.xyz); v_t = qrot(i_q, a_tangent.xyz); v_bs = a_tangent.w < 0.0 ? -1.0 : 1.0;
    v_uv = a_uv; v_layer = i_ex.y; v_fade = i_ex.x; v_moss = i_ex.z; v_tint = i_ex.w;
    v_hfrac = a_pos.y / max(i_misc.x, 0.01);
    vec4 clip = u_viewproj * vec4(wp, 1.0);
    v_w = clip.w; gl_Position = scene_clip(clip);
}
