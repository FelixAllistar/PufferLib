#version 330
#include "depth.glsl"
// Nest mounds, sticks and eggs (render/nest.c): positions relative to the
// nest's centre on the ground or the egg's (u_model turns and moves, never
// scales), a_uv carries the material (x) and ambient occlusion (y).
in vec3 a_pos;
in vec2 a_uv;
in vec3 a_normal;
uniform mat4 u_viewproj, u_model;
out vec3 v_world, v_n; out vec2 v_k; out float v_w;
void main() {
    vec4 wp = u_model * vec4(a_pos, 1.0);
    v_world = wp.xyz; v_n = mat3(u_model) * a_normal; v_k = a_uv;
    vec4 clip = u_viewproj * wp; v_w = clip.w; gl_Position = scene_clip(clip);
}
