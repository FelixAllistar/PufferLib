#version 330
#include "depth.glsl"
in vec3 a_pos;
in vec3 a_normal;
uniform mat4 u_viewproj, u_model;
out vec3 v_world, v_n; out float v_w;
void main() {
    vec4 wp = u_model * vec4(a_pos, 1.0);
    v_world = wp.xyz; v_n = normalize(mat3(u_model) * a_normal);
    vec4 clip = u_viewproj * wp; v_w = clip.w; gl_Position = scene_clip(clip);
}
