#version 330
#include "depth.glsl"
// Skinned dinosaurs (render/dino.c): joint matrices for every agent live in
// t_pose (3 texels per joint = rows of a 3x4 matrix, one row per agent) and
// already include the body transform, so skinning lands in world space.
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_normal;
layout(location = 3) in vec4 a_tangent;   // w = bitangent sign
layout(location = 4) in uvec4 a_joints;
layout(location = 5) in vec4 a_weights;
layout(location = 6) in vec4 i_inst;      // pose row, fade, tint seed, water level (-1e4: dry)
uniform mat4 u_viewproj;
uniform sampler2D t_pose;
out vec3 v_world, v_n, v_t;
out vec2 v_uv;
out float v_w, v_bs, v_tint, v_water, v_fade;
void main() {
    int row = int(i_inst.x + 0.5);
    vec4 r0 = vec4(0.0), r1 = vec4(0.0), r2 = vec4(0.0);
    for (int k = 0; k < 4; k++) {
        float w = a_weights[k];
        if (w <= 0.0) continue;
        int j = int(a_joints[k]) * 3;
        r0 += w * texelFetch(t_pose, ivec2(j, row), 0);
        r1 += w * texelFetch(t_pose, ivec2(j + 1, row), 0);
        r2 += w * texelFetch(t_pose, ivec2(j + 2, row), 0);
    }
    vec4 p = vec4(a_pos, 1.0);
    vec3 wp = vec3(dot(r0, p), dot(r1, p), dot(r2, p));
    mat3 R = transpose(mat3(r0.xyz, r1.xyz, r2.xyz));
    v_world = wp; v_n = R * a_normal.xyz; v_t = R * a_tangent.xyz; v_bs = a_tangent.w < 0.0 ? -1.0 : 1.0;
    v_uv = a_uv; v_tint = i_inst.z; v_water = i_inst.w; v_fade = i_inst.y;
    vec4 clip = u_viewproj * vec4(wp, 1.0);
    v_w = clip.w; gl_Position = scene_clip(clip);
}
