#version 330
// Combat effects (render/fx.c): blood droplets (kind 0), ground decals
// (kind 1 + seed / 2: draped over the rendered terrain height), tail trails (kind 2).
#include "depth.glsl"
#include "terrain_height.glsl"
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_col;
layout(location = 3) in float a_kind;
uniform mat4 u_viewproj;
uniform sampler2D t_height;
out vec2 v_uv; out vec4 v_col; out float v_kind; out vec3 v_world;
void main() {
    vec3 p = a_pos;
    if (a_kind >= 1.0 && a_kind < 2.0) p.y = sample_cubic(t_height, p.xz) + 0.04;
    v_uv = a_uv; v_col = a_col; v_kind = a_kind; v_world = p;
    gl_Position = scene_clip(u_viewproj * vec4(p, 1.0));
}
